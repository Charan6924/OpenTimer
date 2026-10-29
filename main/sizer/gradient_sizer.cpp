// gradient_sizer.cpp  (ot-gsizer)
//
// Gradient-guided gate sizing on OpenTimer's differentiable timing.
//
// Each iteration
//   1. runs one backward pass of a smooth late-TNS objective (report_gradients),
//   2. predicts, to first order, the change of that objective for every
//      candidate cell swap of every gate:
//
//        dJ = sum_{input pins p} dJ/dC_p   * (C_p(new)      - C_p(old))
//           + sum_{arcs a, states} dJ/dd_a * (D_a(new; s, C) - D_a(old; s, C))
//                                + dJ/dS_a * (S_a(new; s, C) - S_a(old; s, C))
//
//      where D, S are the Liberty delay/transition tables evaluated at the arc's
//      current input slew s and output load C (the chain rule through a gate
//      size that moves linearly between two library cells),
//   3. applies the swaps with the largest predicted gain as one batch, checks
//      the exact (hard) late TNS with one timing update, and halves the batch
//      until it improves.
//
// The gradient replaces the greedy sizer's one-timing-update-per-candidate
// search: a whole iteration costs one backward pass plus a few timing updates.
// Phase 2 recovers area with downsizes whose predicted loss is negligible,
// in adaptive batches checked against exact TNS and WNS.

#include "sizer_common.hpp"

namespace {

float pin_cap(const ot::Cellpin& cp, ot::Tran rf) {
  if(rf == ot::RISE && cp.rise_capacitance) return *cp.rise_capacitance;
  if(rf == ot::FALL && cp.fall_capacitance) return *cp.fall_capacitance;
  return cp.capacitance.value_or(0.0f);
}

// The timing of `cell` that plays the role of `ref` (same pin, related pin, type, sense).
const ot::Timing* matching_timing(const ot::Cell& cell, const std::string& pin, const ot::Cellpin& ref_pin,
                                  const ot::Timing& ref) {
  auto it = cell.cellpins.find(pin);
  if(it == cell.cellpins.end()) return nullptr;
  const auto& timings = it->second.timings;
  const size_t index = &ref - ref_pin.timings.data();
  auto same = [&] (const ot::Timing& t) {
    return t.related_pin == ref.related_pin && t.type == ref.type && t.sense == ref.sense;
  };
  if(index < timings.size() && same(timings[index])) return &timings[index];
  for(const auto& t : timings) if(same(t)) return &t;
  return nullptr;
}

struct GateView {
  std::vector<const ot::Pin*> inputs;
  std::vector<const ot::Arc*> arcs;      // non-constraint cell arcs ending at the gate's outputs
};

struct Move {
  std::string gate, from, to;
  double gain;         // predicted increase of the smooth objective (TNS is <= 0)
  float area;          // area change
};

}  // namespace

int main(int argc, char* argv[]) {

  sizer::Options opt;
  try { opt = sizer::parse(argc, argv); }
  catch(const std::exception& e) { std::cerr << e.what() << '\n'; sizer::usage(argv[0]); return 1; }

  ot::Timer timer;
  sizer::Log log;
  sizer::load(timer, opt);
  const auto families = sizer::build_families(timer, opt.families);
  const ot::Celllib* libs[2] = {timer.celllib(ot::MIN), timer.celllib(ot::MAX)};
  constexpr float eps = 1e-6f;

  auto late_tns = [&] { return timer.report_tns(ot::MAX).value_or(0.0f); };
  auto late_wns = [&] { return timer.report_wns(ot::MAX).value_or(0.0f); };
  auto out_of_time = [&] { return opt.time_limit > 0 && log.elapsed() > opt.time_limit; };

  // Temperature: 0.1 x the median late output slew of cell pins (the scale of one
  // stage delay) unless given.
  double tau = opt.tau;
  if(tau <= 0) {
    std::vector<float> s;
    for(const auto& [name, pin] : timer.pins()) {
      const auto* cp = pin.cellpin(ot::MAX);
      if(!cp || cp->direction != ot::CellpinDirection::OUTPUT) continue;
      for(auto rf : {ot::RISE, ot::FALL}) if(auto v = pin.slew(ot::MAX, rf); v && *v > 0) s.push_back(*v);
    }
    if(!s.empty()) std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end());
    tau = s.empty() ? 1.0 : 0.1 * s[s.size() / 2];
  }

  ot::GradientOptions gopt;
  gopt.objective = ot::TimingObjective::SMOOTH_TNS;
  gopt.split = ot::MAX;
  gopt.arrival_temperature = tau;
  gopt.objective_temperature = tau;
  gopt.collect_lut_gradients = false;

  size_t sizable = 0;
  for(const auto& [name, gate] : timer.gates()) sizable += families.find(gate.cell_name()) != nullptr;
  std::cout << "gates " << timer.num_gates() << ", sizable " << sizable << ", tau " << tau << std::endl;
  log.line("before", sizer::metrics(timer));

  size_t gradients = 0, updates = 0;
  double gradient_seconds = 0.0;

  // Predicted gain of every candidate swap, from one gradient report.
  auto predict = [&] (const ot::GradientReport& report, bool downsize_only,
                      const std::unordered_map<std::string, float>* slack, float margin) {
    std::vector<const ot::ArcDelayGradient*> arc_grad;
    for(const auto& a : report.arcs) {
      if(a.arc_id >= arc_grad.size()) arc_grad.resize(a.arc_id + 1, nullptr);
      arc_grad[a.arc_id] = &a;
    }
    std::unordered_map<std::string, const ot::PinParameterGradient*> pin_grad;
    pin_grad.reserve(report.pins.size());
    for(const auto& p : report.pins) pin_grad[p.pin] = &p;

    std::unordered_map<std::string, GateView> view;
    for(const auto& [pname, pin] : timer.pins()) {
      const auto* g = pin.gate();
      const auto* cp = pin.cellpin(ot::MAX);
      if(g && cp && cp->direction == ot::CellpinDirection::INPUT) view[g->name()].inputs.push_back(&pin);
    }
    for(const auto& arc : timer.arcs()) {
      if(!arc.is_cell_arc()) continue;
      const auto tv = arc.timing_view();
      const ot::Timing* t = tv[ot::MAX] ? tv[ot::MAX] : tv[ot::MIN];
      if(!t || t->is_constraint()) continue;
      if(const auto* g = arc.to().gate()) view[g->name()].arcs.push_back(&arc);
    }

    std::vector<Move> moves;
    for(const auto& [gname, gate] : timer.gates()) {
      const std::string cur = gate.cell_name();
      const auto* fam = families.find(cur);
      if(!fam) continue;
      if(slack) {
        auto it = slack->find(gname);
        if(it == slack->end() || it->second < margin) continue;
      }
      const auto& v = view[gname];
      const auto pos = std::find(fam->begin(), fam->end(), cur) - fam->begin();

      Move best {gname, cur, cur, 0.0, 0.0f};
      bool have = false;
      for(size_t k = 0; k < fam->size(); ++k) {
        if(int(k) == pos) continue;
        if(downsize_only && int(k) != pos - 1) continue;
        if(std::abs(int(k) - int(pos)) > opt.max_step) continue;
        const std::string& cand = (*fam)[k];
        double dj = 0.0;
        bool ok = true;
        for(auto el : {ot::MIN, ot::MAX}) {
          if(!ok) break;
          const auto& cell = libs[el]->cells.at(cand);
          // Input pin capacitance changes the load of the driving net.
          for(const auto* p : v.inputs) {
            const auto* cp = p->cellpin(el);
            auto nc = cell.cellpins.find(cp->name);
            if(nc == cell.cellpins.end()) { ok = false; break; }
            const auto* g = pin_grad.at(p->name());
            for(auto rf : {ot::RISE, ot::FALL}) dj += g->capacitance[el][rf] * (pin_cap(nc->second, rf) - p->cap(el, rf));
          }
          // Delay and output slew of the gate's own arcs at their current operating points.
          for(const auto* a : v.arcs) {
            if(!ok) break;
            const ot::Timing* t = a->timing_view()[el];
            if(!t) continue;
            const auto* ag = a->idx() < arc_grad.size() ? arc_grad[a->idx()] : nullptr;
            if(!ag) continue;
            const ot::Timing* tn = matching_timing(cell, a->to().cellpin(el)->name, *a->to().cellpin(el), *t);
            if(!tn) { ok = false; break; }
            for(auto frf : {ot::RISE, ot::FALL}) {
              const auto si = a->from().slew(el, frf);
              if(!si) continue;
              for(auto trf : {ot::RISE, ot::FALL}) {
                const auto& dg = ag->value[el][frf][trf];
                const auto& sg = ag->slew[el][frf][trf];
                if(!(dg && *dg != 0.0) && !(sg && *sg != 0.0)) continue;
                const float load = a->to().net() ? a->to().net()->load(el, trf) : 0.0f;
                if(dg && *dg != 0.0) {
                  const auto d0 = t->delay(frf, trf, *si, load);
                  const auto d1 = tn->delay(frf, trf, *si, load);
                  if(d0 && d1) dj += *dg * (double(*d1) - *d0);
                }
                if(sg && *sg != 0.0) {
                  const auto s0 = t->slew(frf, trf, *si, load);
                  const auto s1 = tn->slew(frf, trf, *si, load);
                  if(s0 && s1) dj += *sg * (double(*s1) - *s0);
                }
              }
            }
          }
        }
        if(!ok) continue;
        const float da = sizer::cell_area(timer, cand) - sizer::cell_area(timer, cur);
        if(downsize_only) { best = {gname, cur, cand, dj, da}; have = true; }
        else if(dj > best.gain) { best = {gname, cur, cand, dj, da}; have = true; }
      }
      if(have) moves.push_back(best);
    }
    return moves;
  };

  auto gradient = [&] () {
    const auto a = sizer::Clock::now();
    auto r = timer.report_gradients(gopt);
    gradient_seconds += std::chrono::duration<double>(sizer::Clock::now() - a).count();
    ++gradients;
    return r;
  };

  // ---- phase 1: fix late (setup) violations -----------------------------------------------
  {
    float tns = late_tns();
    size_t batch = 0;    // adaptive number of moves per iteration
    std::set<std::string> tabu;   // gates whose best predicted move failed the exact check
    for(int iter = 0; iter < opt.max_iters && tns < 0.0f && !out_of_time(); ++iter) {
      auto all = predict(gradient(), false, nullptr, 0.0f);
      std::vector<Move> moves;
      for(auto& mv : all) if(!tabu.count(mv.gate)) moves.push_back(std::move(mv));
      std::sort(moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return a.gain > b.gain; });
      if(moves.empty()) break;
      if(batch == 0) batch = std::max<size_t>(1, moves.size() / 10);
      size_t m = std::min(batch, moves.size());

      for(size_t i = 0; i < m; ++i) timer.repower_gate(moves[i].gate, moves[i].to);
      bool accepted = false;
      while(true) {
        const float t = late_tns();
        ++updates;
        if(t > tns + eps) { tns = t; accepted = true; break; }
        if(m == 1) { timer.repower_gate(moves[0].gate, moves[0].from); break; }
        const size_t keep = m / 2;
        for(size_t i = keep; i < m; ++i) timer.repower_gate(moves[i].gate, moves[i].from);
        m = keep;
      }
      // The best predicted move failed on its own: skip that gate from now on and
      // try the next-best moves one at a time.
      for(size_t i = 0; !accepted && i < std::min<size_t>(8, moves.size()) && !out_of_time(); ++i) {
        tabu.insert(moves[i].gate);
        if(i == 0) continue;
        timer.repower_gate(moves[i].gate, moves[i].to);
        const float t = late_tns();
        ++updates;
        if(t > tns + eps) { tns = t; accepted = true; m = 1; std::swap(moves[0], moves[i]); }
        else timer.repower_gate(moves[i].gate, moves[i].from);
      }
      log.line("fix iter " + std::to_string(iter), sizer::metrics(timer),
               "candidates " + std::to_string(moves.size()) + "  applied " + std::to_string(accepted ? m : 0) +
               "  predicted gain " + std::to_string(moves.front().gain));
      if(!accepted) break;
      batch = m == batch ? std::min(batch * 2, moves.size()) : std::max<size_t>(1, m);
    }
  }

  // ---- phase 2: area recovery with predicted-safe downsizes -----------------------------------
  if(opt.recover_area && !out_of_time()) {
    const float period = sizer::clock_period(timer);
    const float margin = 0.05f * period;
    float tns = late_tns(), wns = late_wns();
    size_t downsized = 0, rounds = 0;
    for(; rounds < 3 && !out_of_time(); ++rounds) {
      const auto slack = sizer::gate_output_slacks(timer);
      auto moves = predict(gradient(), true, &slack, margin);
      // Predicted loss must be small next to the current TNS; largest area saving first.
      const double tol = 1e-4 * std::max(1.0, double(std::abs(tns)));
      std::vector<Move> safe;
      for(const auto& mv : moves) if(mv.gain > -tol && mv.area < 0) safe.push_back(mv);
      std::sort(safe.begin(), safe.end(), [] (const Move& a, const Move& b) { return a.area < b.area; });
      if(safe.empty()) break;

      size_t batch = std::max<size_t>(16, safe.size() / 4), pos = 0, round_down = 0;
      while(pos < safe.size() && !out_of_time()) {
        const size_t end = std::min(safe.size(), pos + batch);
        for(size_t i = pos; i < end; ++i) timer.repower_gate(safe[i].gate, safe[i].to);
        const float t = late_tns(), w = late_wns();
        ++updates;
        if(t >= tns - eps && w >= wns - eps) {
          tns = t; wns = w; round_down += end - pos; pos = end;
          batch = std::min(batch * 2, safe.size());
        }
        else {
          for(size_t i = pos; i < end; ++i) timer.repower_gate(safe[i].gate, safe[i].from);
          if(batch == 1) ++pos;
          else batch = std::max<size_t>(1, batch / 4);
        }
      }
      downsized += round_down;
      timer.update_timing();
      log.line("area round " + std::to_string(rounds), sizer::metrics(timer),
               "downsized " + std::to_string(round_down) + " of " + std::to_string(safe.size()) + " predicted-safe");
      if(round_down == 0) break;
    }
  }

  log.line("after", sizer::metrics(timer),
           "gradients " + std::to_string(gradients) + " (" + std::to_string(int(gradient_seconds)) +
           " s)  timing updates " + std::to_string(updates));
  sizer::write_outputs(timer, opt);
  return 0;
}
