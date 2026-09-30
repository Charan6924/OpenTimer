// Test-only access to Timer's internal forward workspace.
#include <ot/headerdef.hpp>
#define private public
#include <ot/timer/gate.hpp>
#include <ot/timer/pin.hpp>
#include <ot/timer/arc.hpp>
#include <ot/timer/net.hpp>
#include <ot/timer/test.hpp>
#include <ot/timer/clock.hpp>
#include <ot/timer/endpoint.hpp>
#include <ot/timer/path.hpp>
#include <ot/timer/sfxt.hpp>
#include <ot/timer/pfxt.hpp>
#include <ot/timer/cppr.hpp>
#include <ot/timer/scc.hpp>
#include <ot/static/logger.hpp>
#include <ot/spef/spef.hpp>
#include <ot/verilog/verilog.hpp>
#include <ot/sdc/sdc.hpp>
#include <ot/tau/tau15.hpp>
#include <ot/timer/gradient.hpp>
#include <ot/timer/timer.hpp>
#undef private

int main(int argc, char** argv) {
  using ot::SPLIT_TRAN;
  using ot::SPLIT_TRANx2;
  using ot::TRAN;
  const std::string design = argc > 1 ? argv[1] : "simple";
  const std::string prefix = "example/" + design + "/";
  ot::Timer timer;
  if(design == "incremental") {
    timer.read_celllib(prefix + "osu018_stdcells.lib", ot::MIN);
    timer.read_celllib(prefix + "osu018_stdcells.lib", ot::MAX);
  timer.insert_primary_input("inp1");
  timer.insert_primary_input("inp2");
  timer.insert_primary_input("tau2015_clk");
  timer.insert_primary_output("out");

  // // Start wires
  // wire n1;
  // wire n2;
  // wire n3;
  // wire n4;
  // wire inp1;
  // wire inp2;
  // wire tau2015_clk;
  // wire out;
  timer.insert_net("n1");
  timer.insert_net("n2");
  timer.insert_net("n3");
  timer.insert_net("n4");
  timer.insert_net("inp1");
  timer.insert_net("inp2");
  timer.insert_net("tau2015_clk");
  timer.insert_net("out");

  // // Start cells
  // NAND2X1 u1 ( .A(inp1), .B(inp2), .Y(n1) );
  timer.insert_gate("u1", "NAND2X1");
  timer.connect_pin("u1:A", "inp1");
  timer.connect_pin("u1:B", "inp2");
  timer.connect_pin("u1:Y", "n1");
  
  // DFFNEGX1 f1 ( .D(n2), .CLK(tau2015_clk), .Q(n3) );
  timer.insert_gate("f1", "DFFNEGX1");
  timer.connect_pin("f1:D", "n2");
  timer.connect_pin("f1:CLK", "tau2015_clk");
  timer.connect_pin("f1:Q", "n3");
  
  // INVX1 u2 ( .A(n3), .Y(n4) );
  timer.insert_gate("u2", "INVX1");
  timer.connect_pin("u2:A", "n3");
  timer.connect_pin("u2:Y", "n4");
  
  // INVX2 u3 ( .A(n4), .Y(out) );
  timer.insert_gate("u3", "INVX2");
  timer.connect_pin("u3:A", "n4");
  timer.connect_pin("u3:Y", "out");
  
  // NOR2X1 u4 ( .A(n1), .B(n3), .Y(n2) );
  timer.insert_gate("u4", "NOR2X1");
  timer.connect_pin("u4:A", "n1");
  timer.connect_pin("u4:B", "n3");
  timer.connect_pin("u4:Y", "n2");

  // the 3-rd step is to assert timing constraints
  // timer.read_sdc("simple.sdc");  <-- of course you can do this.
  //create_clock -period 50 -name tau2015_clk [get_ports tau2015_clk]
  timer.create_clock("tau2015_clk", "tau2015_clk", 50);

  // assert input arrival time and transition for PIs
  // set_input_delay 0 -min -rise [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_delay 25 -min -fall [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_delay 0 -max -rise [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_delay 25 -max -fall [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_transition 10 -min -rise [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_transition 15 -min -fall [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_transition 10 -max -rise [get_ports tau2015_clk] -clock tau2015_clk
  // set_input_transition 15 -max -fall [get_ports tau2015_clk] -clock tau2015_clk
  timer.set_at("tau2015_clk", ot::MIN, ot::RISE, 0);
  timer.set_at("tau2015_clk", ot::MIN, ot::FALL, 25);
  timer.set_at("tau2015_clk", ot::MAX, ot::RISE, 0);
  timer.set_at("tau2015_clk", ot::MAX, ot::FALL, 25);
  timer.set_slew("tau2015_clk", ot::MIN, ot::RISE, 10);
  timer.set_slew("tau2015_clk", ot::MIN, ot::FALL, 15);
  timer.set_slew("tau2015_clk", ot::MAX, ot::RISE, 10);
  timer.set_slew("tau2015_clk", ot::MAX, ot::FALL, 15);

  // similarly, assert arrival time for inp1 and inp2
  timer.set_at("inp1", ot::MIN, ot::RISE, 0);
  timer.set_at("inp1", ot::MIN, ot::FALL, 0);
  timer.set_at("inp1", ot::MAX, ot::RISE, 5);
  timer.set_at("inp1", ot::MAX, ot::FALL, 5);
  timer.set_slew("inp1", ot::MIN, ot::RISE, 10);
  timer.set_slew("inp1", ot::MIN, ot::FALL, 15);
  timer.set_slew("inp1", ot::MAX, ot::RISE, 20);
  timer.set_slew("inp1", ot::MAX, ot::FALL, 25);
  timer.set_at("inp2", ot::MIN, ot::RISE, 0);
  timer.set_at("inp2", ot::MIN, ot::FALL, 0);
  timer.set_at("inp2", ot::MAX, ot::RISE, 1);
  timer.set_at("inp2", ot::MAX, ot::FALL, 1);
  timer.set_slew("inp2", ot::MIN, ot::RISE, 30);
  timer.set_slew("inp2", ot::MIN, ot::FALL, 30);
  timer.set_slew("inp2", ot::MAX, ot::RISE, 40);
  timer.set_slew("inp2", ot::MAX, ot::FALL, 40);

  // for output, we assert required arrival time and load capacitance
  // notice that sdc has a different definition for required arrival time
  // set_load -pin_load 4 [get_ports out]
  // set_output_delay -10 -min -rise [get_ports out] -clock tau2015_clk
  // set_output_delay -10 -min -fall [get_ports out] -clock tau2015_clk
  // set_output_delay 30 -max -rise [get_ports out] -clock tau2015_clk
  // set_output_delay 30 -max -fall [get_ports out] -clock tau2015_clk
  timer.set_load("out", ot::MIN, ot::RISE, 4);
  timer.set_load("out", ot::MIN, ot::FALL, 4);
  timer.set_load("out", ot::MAX, ot::RISE, 4);
  timer.set_load("out", ot::MAX, ot::FALL, 4);
  timer.set_rat("out", ot::MIN, ot::RISE, 10);
  timer.set_rat("out", ot::MIN, ot::FALL, 10);
  timer.set_rat("out", ot::MAX, ot::RISE, 20);
  timer.set_rat("out", ot::MAX, ot::FALL, 20);

  } else {
    std::string early = "osu018_stdcells.lib", late = early;
    if(design == "map9v3") early = late = "osu018_stdcells";
    if(design == "sizer") early = late = "NangateOpenCellLibrary_typical.lib";
    if(design == "optimizer") { early="optimizer_Early.lib"; late="optimizer_Late.lib"; }
    timer.read_celllib(prefix+early, ot::MIN).read_celllib(prefix+late, ot::MAX)
      .read_verilog(prefix+design+".v").read_sdc(prefix+design+".sdc");
    if(design=="unit" || design=="map9v3" || design=="sizer" || design=="optimizer")
      timer.read_spef(prefix+design+".spef");
  }
  timer.update_timing();
  std::ostringstream before;
  timer.dump_at(before);
  std::ostringstream before_rat;
  timer.dump_rat(before_rat);
  ot::GradientOptions options;
  options.arrival_temperature = 0.1;
  options.objective_temperature = 0.1;
  size_t checked = 0, merges = 0, singles = 0;
  std::vector<ot::Timer::GradientPinData> previous;
  for(double temperature : {0.1, 0.01, 0.001}) {
    options.arrival_temperature = temperature;
    double max_gap[2] {0.0, 0.0};
    double max_percent[2] {0.0, 0.0};
    std::string max_gap_pin[2];
    ot::Timer::GradientContext context(options);
    timer._update_timing(&context);
    for(const auto& entry : timer._pins) {
      const auto& pin = entry.second;
      FOR_EACH_EL_RF(el, trf) {
        std::vector<double> candidates;
        std::vector<double> hard_candidates;
        if(auto pi = pin.primary_input(); pi && pi->_at[el][trf]) {
          candidates.push_back(*pi->_at[el][trf]);
          hard_candidates.push_back(*pi->_at[el][trf]);
        }
        double weight_sum = 0.0;
        for(auto arc : pin._fanin) {
          if(arc->is_loop_breaker()) continue;
          FOR_EACH_RF(frf) {
            const auto& source = context.smooth_arrivals[arc->from().idx()][el][frf];
            const auto& delay = arc->_delay[el][frf][trf];
            const auto& hard_source = arc->from()._at[el][frf];
            if(hard_source && delay) {
              hard_candidates.push_back(hard_source->numeric + *delay);
            }
            if(source && delay) {
              candidates.push_back(*source + *delay);
              auto weight = context.reduction_weights[arc->idx()][el][frf][trf];
              if(!weight || *weight < 0.0 || *weight > 1.0) return 1;
              weight_sum += *weight;
            }
          }
        }
        const auto& actual = context.smooth_arrivals[pin.idx()][el][trf];
        if(candidates.empty()) { if(actual) return 2; continue; }
        const double sign = el == ot::MAX ? 1.0 : -1.0;
        // Independent hard reduction using exact source arrivals.
        if(hard_candidates.empty() || !pin._at[el][trf]) return 7;
        const double hard = el == ot::MAX
          ? *std::max_element(hard_candidates.begin(), hard_candidates.end())
          : *std::min_element(hard_candidates.begin(), hard_candidates.end());
        if(std::abs(hard - pin._at[el][trf]->numeric) > 1e-3) return 8;
        const double gap = sign * (*actual - pin._at[el][trf]->numeric);
        const double exact = pin._at[el][trf]->numeric;
        if(exact != 0.0) {
          max_percent[el] = std::max(max_percent[el],
            100.0 * std::abs(*actual - exact) / std::abs(exact));
        }
        // Exact arrivals accumulate in float, smooth arrivals in double.
        if(gap < -1e-3) return 9;
        if(gap > max_gap[el]) {
          max_gap[el] = gap;
          max_gap_pin[el] = pin.name();
        }
        if(!previous.empty() && previous[pin.idx()][el][trf]) {
          if(sign * (*actual - *previous[pin.idx()][el][trf]) > 1e-10) return 10;
        }
        // Stable independent long-double logsumexp reference.
        long double anchor = -std::numeric_limits<long double>::infinity();
        for(double value : candidates) anchor = std::max(anchor, (long double)(sign * value));
        long double sum = 0.0L;
        for(double value : candidates) sum += std::exp((sign * value - anchor) / temperature);
        const double expected = sign * (anchor + temperature * std::log(sum));
        if(!actual || std::abs(*actual - expected) > 1e-10) return 3;
        if(!pin.primary_input() && std::abs(weight_sum - 1.0) > 1e-12) return 4;
        ++checked;
        if(candidates.size() == 1) ++singles; else ++merges;
      }
    }
    previous = context.smooth_arrivals;
    size_t endpoint_count = 0;
    double largest_slack_difference = 0.0;
    auto compare_slack = [&](const std::string& name, ot::Split el, ot::Tran rf,
                             double exact, double smooth) {
      const double difference = smooth - exact;
      if(!std::isfinite(smooth)) throw std::runtime_error("nonfinite smooth slack");
      largest_slack_difference = std::max(largest_slack_difference, std::abs(difference));
      ++endpoint_count;
      std::cout << std::setprecision(10) << "ENDPOINT," << design << ','
                << temperature << ',' << name << ',' << ot::to_string(el) << ','
                << ot::to_string(rf) << ',' << exact << ',' << smooth << ','
                << difference << '\n';
    };
    FOR_EACH_EL_RF(el, rf) {
      for(const auto& entry : timer._pos) {
        const auto& po = entry.second;
        const auto exact = po.slack(el, rf);
        const auto& data = context.smooth_arrivals[po._pin.idx()][el][rf];
        if(exact && data && po._rat[el][rf]) {
          compare_slack(entry.first, el, rf, *exact,
            el == ot::MIN ? *data - *po._rat[el][rf] : *po._rat[el][rf] - *data);
        }
      }
      for(const auto& test : timer._tests) {
        const auto exact = test.slack(el, rf);
        const auto tv = test._arc.timing_view();
        if(!exact || !tv[el] || !test._constraint[el][rf] || timer._clocks.empty()) continue;
        const ot::Tran crf = tv[el]->is_rising_edge_triggered() ? ot::RISE : ot::FALL;
        const auto& clock = context.smooth_arrivals[test.related_pin().idx()]
          [el == ot::MIN ? ot::MAX : ot::MIN][crf];
        const auto& data = context.smooth_arrivals[test.constrained_pin().idx()][el][rf];
        if(!clock || !data) continue;
        const double required = el == ot::MIN
          ? *clock + *test._constraint[el][rf]
          : *clock + timer._clocks.begin()->second.period() - *test._constraint[el][rf];
        compare_slack(test.constrained_pin().name(), el, rf, *exact,
          el == ot::MIN ? *data - required : required - *data);
      }
    }
    std::cout << "Endpoint comparison: " << endpoint_count << " states, largest absolute slack difference="
              << largest_slack_difference << " ns\n";
    for(auto objective : {ot::TimingObjective::TNS, ot::TimingObjective::WNS}) {
      options.objective = objective;
      timer._update_timing(&context);
      const auto arc_gradients = context.arc_gradients;
      const auto report = timer.report_gradients(options);
      if(report.arcs.size() != timer._arcs.size() || !report.objective_value ||
         std::abs(*report.objective_value - *context.objective_value) > 1e-10) return 14;
      for(const auto& arc : report.arcs) {
        FOR_EACH_EL_RF_RF(el, frf, trf) {
          if(arc.value[el][frf][trf] != arc_gradients[arc.arc_id][el][frf][trf]) return 15;
        }
      }
      auto evaluate = [&]() {
        tf::Taskflow forward;
        std::vector<tf::Task> tasks(timer._idx2pin.size());
        for(auto& entry : timer._pins) {
          auto* pin = &entry.second;
          tasks[pin->idx()] = forward.emplace([&, pin]() {
            timer._smooth_fprop_at(*pin, context);
          });
        }
        for(const auto& arc : timer._arcs) {
          if(!arc.is_loop_breaker()) {
            tasks[arc.from().idx()].precede(tasks[arc.to().idx()]);
          }
        }
        timer._executor.run(forward).wait();
        timer._seed_gradient_objective(context);
        return *context.objective_value;
      };
      size_t delay_checks = 0, delay_index = 0;
      size_t valid_delays = 0;
      for(const auto& arc : timer._arcs) { FOR_EACH_EL_RF_RF(el, frf, trf) {
        if(arc._delay[el][frf][trf]) ++valid_delays;
      }}
      const size_t stride = std::max(size_t(1), valid_delays / 128);
      for(auto& arc : timer._arcs) {
        FOR_EACH_EL_RF_RF(el, frf, trf) {
          auto& delay = arc._delay[el][frf][trf];
          if(!delay) continue;
          if(delay_index++ % stride != 0) continue;
          const float original = *delay;
          const float plus_delay = original + 0.001f;
          const float minus_delay = original - 0.001f;
          delay = plus_delay;
          const double plus = evaluate();
          delay = minus_delay;
          const double minus = evaluate();
          delay = original;
          const double numeric = (plus - minus) / (double(plus_delay) - double(minus_delay));
          const double analytic = arc_gradients[arc.idx()][el][frf][trf].value_or(0.0);
          if(std::abs(numeric - analytic) > 1e-4) {
            std::cerr << "Arc derivative mismatch: " << arc.name() << " analytic="
                      << analytic << " numeric=" << numeric << '\n';
            return 13;
          }
          ++delay_checks;
        }
      }
      evaluate();
      std::cout << "PASS: " << delay_checks << " arc-delay finite differences\n";
      timer._seed_gradient_objective(context);
      if(!context.objective_value) return 11;
      const auto seeds = context.pin_adjoints;
      for(auto& entry : timer._pins) {
        auto& pin = entry.second;
        FOR_EACH_EL_RF(el, rf) {
          auto& arrival = context.smooth_arrivals[pin.idx()][el][rf];
          if(!arrival) continue;
          const double original = *arrival;
          const double h = 1e-6;
          arrival = original + h;
          timer._seed_gradient_objective(context);
          const double plus = *context.objective_value;
          arrival = original - h;
          timer._seed_gradient_objective(context);
          const double minus = *context.objective_value;
          arrival = original;
          const double numeric = (plus - minus) / (2 * h);
          if(std::abs(numeric - seeds[pin.idx()][el][rf].value_or(0.0)) > 1e-4) {
            std::cerr << "Objective seed mismatch: " << pin.name() << '\n';
            return 12;
          }
        }
      }
      timer._seed_gradient_objective(context);
      std::cout << (objective == ot::TimingObjective::TNS ? "TNS=" : "WNS=")
                << *context.objective_value << "; endpoint seeds pass finite differences\n";
    }
    std::cout << "tau=" << temperature << " ns: largest MIN gap="
              << max_gap[ot::MIN] << " ns (" << max_gap_pin[ot::MIN]
              << "), MAX gap=" << max_gap[ot::MAX] << " ns ("
              << max_gap_pin[ot::MAX] << ")\n";
    std::cout << "Maximum relative error: MIN=" << max_percent[ot::MIN]
              << "%, MAX=" << max_percent[ot::MAX] << "%\n";
  }
  size_t filter_checks = 0;
  for(auto objective : {ot::TimingObjective::TNS, ot::TimingObjective::WNS}) {
    options.objective = objective;
    for(int s=-1; s<2; ++s) for(int t=-1; t<2; ++t) {
      options.split = s < 0 ? std::optional<ot::Split>{} : std::optional<ot::Split>{static_cast<ot::Split>(s)};
      options.transition = t < 0 ? std::optional<ot::Tran>{} : std::optional<ot::Tran>{static_cast<ot::Tran>(t)};
      timer.set_num_threads(1);
      const auto serial = timer.report_gradients(options);
      timer.set_num_threads(4);
      const auto parallel = timer.report_gradients(options);
      if(serial.objective_value.has_value() != parallel.objective_value.has_value()) return 20;
      if(serial.objective_value && std::abs(*serial.objective_value-*parallel.objective_value)>1e-8) return 21;
      for(size_t i=0; i<serial.arcs.size(); ++i) {
        FOR_EACH_EL_RF_RF(el, frf, trf) {
          auto x=serial.arcs[i].value[el][frf][trf], y=parallel.arcs[i].value[el][frf][trf];
          if(x.has_value()!=y.has_value() || (x && std::abs(*x-*y)>1e-8)) return 22;
          if(x && !std::isfinite(*x)) return 23;
        }
      }
      ++filter_checks;
    }
  }
  std::cout << "PASS: " << filter_checks << " filtered serial/parallel reports\n";
  std::ostringstream after;
  timer.dump_at(after);
  if(before.str() != after.str()) return 5;
  std::ostringstream after_rat;
  timer.dump_rat(after_rat);
  if(before_rat.str() != after_rat.str()) return 7;
  options.split.reset(); options.transition.reset();
  if(design == "optimizer" || design == "sizer") {
    if(design == "optimizer") {
      timer.repower_gate("inst_10", "INV_X16").insert_gate("TAUGATE_1", "BUF_X2")
        .insert_net("TAUNET_1").disconnect_pin("inst_3:ZN")
        .connect_pin("inst_3:ZN", "TAUNET_1").connect_pin("TAUGATE_1:A", "TAUNET_1")
        .connect_pin("TAUGATE_1:Z", "net_14").read_spef(prefix+"change_1.spef");
    } else {
      for(int i=0;i<6;++i) timer.repower_gate("inst_"+std::to_string(i), "NAND2_X1");
    }
    const auto changed = timer.report_gradients(options);
    if(!changed.objective_value || changed.arcs.size()!=timer._arcs.size()) return 24;
    std::ostringstream changed_at, changed_rat;
    timer.dump_at(changed_at); timer.dump_rat(changed_rat);
    timer._insert_full_timing_frontiers();
    timer._lineage = timer._taskflow.emplace([](){});
    timer.update_timing();
    std::ostringstream rebuilt_at, rebuilt_rat;
    timer.dump_at(rebuilt_at); timer.dump_rat(rebuilt_rat);
    if(changed_at.str()!=rebuilt_at.str() || changed_rat.str()!=rebuilt_rat.str()) return 25;
    std::cout << "PASS: pending sizing/topology edits and exact full rebuild agree\n";
  }
  if(!singles || !merges) return 6;
  std::cout << "PASS: " << checked << " states, " << singles
            << " single candidates, " << merges
            << " merges; repeated queries and exact arrivals/RAT preserved\n";
}
