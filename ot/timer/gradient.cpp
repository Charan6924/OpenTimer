#include <ot/timer/timer.hpp>

namespace ot {
namespace {

void add(double& target, double value) {
  target = GradientContext::require_finite(target + value, "model adjoint must be finite");
}
void add(std::optional<double>& target, double value) {
  target = GradientContext::require_finite(target.value_or(0.0) + value,
    "model adjoint must be finite");
}

void accumulate_lut(GradientContext& context, const Lut* lut,
                    const LutDerivatives& local, double seed) {
  if(!context.options.collect_lut_gradients) return;
  if(!lut) throw std::runtime_error("missing LUT for a model derivative");
  std::scoped_lock lock(context.model_adjoints_mutex);
  auto& values = context.lut_adjoints[lut];
  if(values.empty()) values.resize(lut->table.size(), 0.0);
  for(size_t i = 0; i < local.count; ++i) {
    add(values.at(local.table_indices[i]), seed * local.table_weights[i]);
  }
}

}  // namespace

// Constraint seeds are available before any reverse pin task starts.
void Timer::_gradient_seed_constraints(GradientContext& context) {
  for(const auto& test : _tests) {
    auto seeds = context.constraint_adjoints.find(&test);
    if(seeds == context.constraint_adjoints.end()) continue;
    const auto tv = test._arc.timing_view();
    FOR_EACH_EL_RF(el, rf) {
      const auto seed = seeds->second[el][rf];
      if(!seed) continue;
      const Split cel = el == MIN ? MAX : MIN;
      const Tran crf = tv[el]->is_rising_edge_triggered() ? RISE : FALL;
      const float related = *context.smooth_slew(&test._arc._from, cel, crf, true);
      const float constrained = *context.smooth_slew(&test._arc._to, el, rf, true);
      const auto& local = context.constraint_derivatives.at(tv[el]).at({crf, rf, related, constrained});
      add(context.slew_adjoints[test._arc._from.idx()][cel][crf], *seed * local.drelated_slew);
      add(context.slew_adjoints[test._arc._to.idx()][el][rf], *seed * local.dconstrained_slew);
      accumulate_lut(context, local.lut, local.interpolation, *seed);
    }
  }
}

// Each destination owns its incoming arc records. Sources gather them after
// all downstream tasks finish, just as they do for arrival adjoints.
void Timer::_gradient_bprop_models(Pin& pin, GradientContext& context) {
  auto& slew_adjoints = context.slew_adjoints[pin.idx()];
  FOR_EACH_EL_RF(el, rf) {
    if(!context.smooth_slew(&pin, el, rf)) continue;
    double value = slew_adjoints[el][rf].value_or(0.0);
    for(auto arc : pin._fanout) {
      if(arc->is_loop_breaker()) continue;
      FOR_EACH_RF(trf) {
        add(value, context.arc_slew_contributions[arc->idx()][el][rf][trf].value_or(0.0));
      }
    }
    slew_adjoints[el][rf] = value;
  }

  for(auto arc : pin._fanin) {
    if(arc->is_loop_breaker()) continue;
    const auto tv = arc->is_cell_arc() ? arc->timing_view() : TimingView {};
    const auto delays = context.smooth_delays.find(arc);
    FOR_EACH_EL_RF_RF(el, frf, trf) {
      const double delay_seed = context.arc_gradients[arc->idx()][el][frf][trf].value_or(0.0);
      double slew_seed = 0.0;
      bool has_slew_candidate = false;
      auto states = context.smooth_slews.find(&pin);
      if(states != context.smooth_slews.end()) {
        for(const auto& candidate : states->second[el][trf].candidates) {
          if(candidate.arc == arc && candidate.source_split == el && candidate.source_transition == frf) {
            has_slew_candidate = true;
            add(slew_seed, slew_adjoints[el][trf].value_or(0.0) * candidate.weight);
          }
        }
      }
      const bool has_delay = delays != context.smooth_delays.end() && delays->second[el][frf][trf];
      if(!has_delay && !has_slew_candidate) continue;
      // A net delay depends on RC values even when no input slew is supplied.
      const float si = arc->is_cell_arc() || has_slew_candidate
        ? *context.smooth_slew(&arc->_from, el, frf, true) : 0.0f;
      double source_slew = 0.0;
      double load = 0.0;
      if(arc->is_cell_arc()) {
        const float capacitance = pin._net ? pin._net->_load(el, trf) : 0.0f;
        const auto key = GradientContext::CellDelayKey {frf, trf, si, capacitance};
        if(has_delay) {
          const auto& local = context.cell_delay_derivatives.at(tv[el]).at(key);
          add(source_slew, delay_seed * local.dinput_slew);
          add(load, delay_seed * local.dload);
          accumulate_lut(context, local.lut, local.interpolation, delay_seed);
        }
        if(has_slew_candidate) {
          const auto& local = context.cell_slew_derivatives.at(tv[el]).at(key);
          add(source_slew, slew_seed * local.dinput_slew);
          add(load, slew_seed * local.dload);
          accumulate_lut(context, local.lut, local.interpolation, slew_seed);
        }
      }
      else {
        if(frf != trf) throw std::runtime_error("net arc changes transition in gradient pass");
        auto* net = std::get<Net*>(arc->_handle);
        if(auto* tree = std::get_if<Rct>(&net->_rct)) {
          auto* node = tree->_node(pin.name());
          if(!node) throw std::runtime_error("missing RC sink node in gradient pass");
          if(has_slew_candidate) {
            const auto& local = context.rc_slew_derivatives.at(node).at({el, frf, si});
            source_slew = slew_seed * local.dinput_slew;
            context.arc_impulse_contributions[arc->idx()][el][frf][trf] =
              GradientContext::require_finite(slew_seed * local.dimpulse, "RC impulse adjoint must be finite");
          }
        }
        else {
          source_slew = slew_seed;  // Empty RC model passes input slew through.
        }
      }
      context.arc_slew_contributions[arc->idx()][el][frf][trf] =
        GradientContext::require_finite(source_slew, "source slew contribution must be finite");
      context.arc_load_contributions[arc->idx()][el][frf][trf] = load;
    }
  }
}

// All pin tasks have finished. Shared model contributions can now be reduced
// in a fixed order and each RC tree reversed without cross-pin writes.
void Timer::_gradient_finalize_models(GradientContext& context) {
  using StateValues = TimingData<double, MAX_SPLIT, MAX_TRAN>;
  std::unordered_map<const Net*, StateValues> net_loads;
  std::vector<StateValues> pin_caps(_idx2pin.size());
  for(const auto& arc : _arcs) {
    if(arc.is_loop_breaker()) continue;
    FOR_EACH_EL_RF_RF(el, frf, trf) {
      if(arc.is_cell_arc()) {
        if(auto* net = arc._to._net) {
          add(net_loads[net][el][trf],
            context.arc_load_contributions[arc.idx()][el][frf][trf].value_or(0.0));
        }
      }
      else if(frf == trf) {
        auto* net = std::get<Net*>(arc._handle);
        if(auto* tree = std::get_if<Rct>(&net->_rct)) {
          auto* node = tree->_node(arc._to.name());
          if(!node) throw std::runtime_error("missing RC node while reducing model adjoints");
          auto& adjoint = context.rc_trees[tree].adjoints[node][el][trf];
          add(adjoint.delay, context.arc_gradients[arc.idx()][el][frf][trf].value_or(0.0));
          add(adjoint.impulse, context.arc_impulse_contributions[arc.idx()][el][frf][trf].value_or(0.0));
        }
      }
    }
  }

  for(auto& entry : _nets) {
    auto& net = entry.second;
    auto* tree = std::get_if<Rct>(&net._rct);
    if(!tree) {
      for(auto* pin : net._pins) {
        if(pin == net._root || pin->primary_input()) continue;
        FOR_EACH_EL_RF(el, rf) add(pin_caps[pin->idx()][el][rf], net_loads[&net][el][rf]);
      }
      continue;
    }
    if(!tree->_derivative_cache) throw std::runtime_error("missing cached RC derivatives");
    const auto& cache = *tree->_derivative_cache;
    auto& work = context.rc_trees[tree];
    FOR_EACH_EL_RF(el, rf) {
      add(work.adjoints[tree->_root][el][rf].load, net_loads[&net][el][rf]);
      // Impulse = 2 beta - delay squared.
      for(auto* node : cache.traversal) {
        auto& a = work.adjoints[node][el][rf];
        const auto& d = cache.local_derivatives.at(node)[el][rf];
        add(a.beta, a.impulse * d.dimpulse_dbeta);
        add(a.delay, a.impulse * d.dimpulse_ddelay);
      }
      // Beta is computed from parent to child, so reverse it child to parent.
      for(auto it = cache.traversal.rbegin(); it != cache.traversal.rend(); ++it) {
        auto* node = *it;
        const auto& relation = cache.nodes.at(node);
        if(!relation.parent) continue;
        const auto& d = cache.local_derivatives.at(node)[el][rf];
        auto& a = work.adjoints[node][el][rf];
        add(work.adjoints[relation.parent][el][rf].beta, a.beta * d.dbeta_dparent_beta);
        add(work.resistance_adjoints[relation.parent_edge][el][rf], a.beta * d.dbeta_dresistance);
        add(a.load_delay, a.beta * d.dbeta_dload_delay);
      }
      // Load-delay sums were computed child to parent.
      for(auto* node : cache.traversal) {
        auto& a = work.adjoints[node][el][rf];
        const auto& d = cache.local_derivatives.at(node)[el][rf];
        add(a.wire_capacitance, a.load_delay * d.dload_delay_dcap);
        add(a.delay, a.load_delay * d.dload_delay_ddelay);
        for(auto* child : cache.nodes.at(node).children) {
          add(work.adjoints[child][el][rf].load_delay, a.load_delay * d.dload_delay_dchild_load_delay);
        }
      }
      // Reverse Elmore delay, including both direct-delay and impulse paths.
      for(auto it = cache.traversal.rbegin(); it != cache.traversal.rend(); ++it) {
        auto* node = *it;
        const auto& relation = cache.nodes.at(node);
        if(!relation.parent) continue;
        const auto& d = cache.local_derivatives.at(node)[el][rf];
        auto& a = work.adjoints[node][el][rf];
        add(work.adjoints[relation.parent][el][rf].delay, a.delay * d.ddelay_dparent_delay);
        add(work.resistance_adjoints[relation.parent_edge][el][rf], a.delay * d.ddelay_dresistance);
        add(a.load, a.delay * d.ddelay_dload);
      }
      // Reverse downstream load sums and split local wire/pin capacitance.
      for(auto* node : cache.traversal) {
        auto& a = work.adjoints[node][el][rf];
        const auto& relation = cache.nodes.at(node);
        const auto& d = cache.local_derivatives.at(node)[el][rf];
        add(a.wire_capacitance, a.load * d.dload_dcap);
        if(relation.pin && !relation.pin->primary_input()) {
          a.pin_capacitance = a.wire_capacitance;
          add(pin_caps[relation.pin->idx()][el][rf], a.pin_capacitance);
        }
        for(auto* child : relation.children) {
          add(work.adjoints[child][el][rf].load, a.load * d.dload_dchild_load);
        }
      }
    }
    for(auto* node : cache.traversal) {
      double cap = 0.0;
      FOR_EACH_EL_RF(el, rf) add(cap, work.adjoints[node][el][rf].wire_capacitance);
      context.report.rc_capacitances.push_back({net.name(), node->_name, cap});
      const auto& relation = cache.nodes.at(node);
      if(relation.parent_edge) {
        double res = 0.0;
        FOR_EACH_EL_RF(el, rf) add(res, work.resistance_adjoints[relation.parent_edge][el][rf]);
        context.report.rc_resistances.push_back({net.name(), relation.parent->_name, node->_name, res});
      }
    }
  }

  for(const auto& entry : _pins) {
    const auto& pin = entry.second;
    PinParameterGradient result;
    result.pin = pin.name();
    result.primary_input = pin.primary_input() != nullptr;
    result.primary_output = pin.primary_output() != nullptr;
    result.capacitance = pin_caps[pin.idx()];
    if(auto* pi = pin.primary_input()) {
      FOR_EACH_EL_RF(el, rf) {
        if(pi->_at[el][rf] && context.smooth_arrivals[pin.idx()][el][rf]) {
          const double sign = el == MAX ? 1.0 : -1.0;
          const double weight = std::exp(sign * (*pi->_at[el][rf] -
            *context.smooth_arrivals[pin.idx()][el][rf]) / context.options.arrival_temperature);
          result.arrival[el][rf] = GradientContext::require_finite(
            weight * context.pin_adjoints[pin.idx()][el][rf].value_or(0.0), "input arrival gradient must be finite");
        }
        auto states = context.smooth_slews.find(&pin);
        if(states != context.smooth_slews.end()) {
          for(const auto& candidate : states->second[el][rf].candidates) {
            if(!candidate.arc) add(result.slew[el][rf],
              candidate.weight * context.slew_adjoints[pin.idx()][el][rf].value_or(0.0));
          }
        }
      }
    }
    context.report.pins.push_back(std::move(result));
  }
  std::sort(context.report.pins.begin(), context.report.pins.end(),
    [](const auto& a, const auto& b) {return a.pin < b.pin;});

  if(context.options.collect_lut_gradients) {
    std::unordered_map<const Lut*, size_t> indices;
    const auto register_table = [&](const std::optional<Lut>& table, const Arc& arc,
                                    Split el, LutTimingKind kind, Tran rf) {
      if(!table || context.lut_derivatives.find(&*table) == context.lut_derivatives.end()) return;
      auto found = indices.find(&*table);
      if(found == indices.end()) {
        const size_t id = context.report.luts.size();
        indices.emplace(&*table, id);
        LutEntryGradient result;
        result.table_id = id;
        result.name = table->name;
        auto values = context.lut_adjoints.find(&*table);
        result.values = values == context.lut_adjoints.end()
          ? std::vector<double>(table->table.size(), 0.0) : values->second;
        context.report.luts.push_back(std::move(result));
        found = indices.find(&*table);
      }
      context.report.luts[found->second].uses.push_back({arc.idx(), el, kind, rf});
    };
    for(const auto& arc : _arcs) {
      if(!arc.is_cell_arc()) continue;
      const auto tv = arc.timing_view();
      FOR_EACH_EL(el) {
        if(!tv[el]) continue;
        register_table(tv[el]->cell_rise, arc, el, LutTimingKind::DELAY, RISE);
        register_table(tv[el]->cell_fall, arc, el, LutTimingKind::DELAY, FALL);
        register_table(tv[el]->rise_transition, arc, el, LutTimingKind::SLEW, RISE);
        register_table(tv[el]->fall_transition, arc, el, LutTimingKind::SLEW, FALL);
        register_table(tv[el]->rise_constraint, arc, el, LutTimingKind::CONSTRAINT, RISE);
        register_table(tv[el]->fall_constraint, arc, el, LutTimingKind::CONSTRAINT, FALL);
      }
    }
  }
}

}  // namespace ot
