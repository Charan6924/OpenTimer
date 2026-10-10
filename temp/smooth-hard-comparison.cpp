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
  using ot::SPLIT;
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

  std::cout << "design,tau,split,arrivals,max_arrival_error,max_arrival_percent,endpoint_count,max_slack_error,hard_tns,smooth_tns,tns_percent,hard_wns,smooth_wns,wns_percent,nonfinite,missing,exact_preserved\n";
  for(double tau : {0.1, 0.01, 0.001}) {
    ot::GradientOptions options;
    options.arrival_temperature = tau;
    options.objective_temperature = tau;
    ot::GradientContext context(options);
    timer._update_timing(&context);
    std::ostringstream after, after_rat;
    timer.dump_at(after); timer.dump_rat(after_rat);
    bool preserved = before.str() == after.str() && before_rat.str() == after_rat.str();
    FOR_EACH_EL(el) {
      size_t arrivals=0, nonfinite=0, missing=0;
      double max_arrival=0, max_percent=0, max_slack=0;
      std::vector<double> hard_slacks, smooth_slacks;
      for(const auto& entry : timer._pins) {
        const auto& pin=entry.second;
        FOR_EACH_RF(rf) {
          const auto& hard=pin._at[el][rf];
          const auto& smooth=context.smooth_arrivals[pin.idx()][el][rf];
          if(bool(hard) != bool(smooth)) ++missing;
          if(!hard || !smooth) continue;
          if(!std::isfinite(*smooth)) { ++nonfinite; continue; }
          ++arrivals;
          double error=std::abs(*smooth-hard->numeric);
          max_arrival=std::max(max_arrival,error);
          if(std::abs(hard->numeric)>1e-9) max_percent=std::max(max_percent,100*error/std::abs(hard->numeric));
        }
      }
      auto endpoint=[&](std::optional<float> hard, std::optional<double> smooth) {
        if(bool(hard)!=bool(smooth)) ++missing;
        if(!hard || !smooth) return;
        if(!std::isfinite(*smooth)) { ++nonfinite; return; }
        hard_slacks.push_back(*hard); smooth_slacks.push_back(*smooth);
        max_slack=std::max(max_slack,std::abs(*smooth-*hard));
      };
      for(const auto& entry : timer._pos) {
        const auto& po=entry.second;
        FOR_EACH_RF(rf) {
          std::optional<double> slack;
          auto at=context.smooth_arrivals[po._pin.idx()][el][rf];
          if(at && po._rat[el][rf]) slack=el==ot::MIN ? *at-*po._rat[el][rf] : *po._rat[el][rf]-*at;
          endpoint(po.slack(el,rf),slack);
        }
      }
      for(const auto& test : timer._tests) {
        auto tv=test._arc.timing_view();
        FOR_EACH_RF(rf) {
          std::optional<double> slack;
          auto found=context.smooth_constraints.find(&test);
          if(!timer._clocks.empty() && tv[el] && found!=context.smooth_constraints.end()) {
            auto constraint=found->second[el][rf];
            auto crf=tv[el]->is_rising_edge_triggered()?ot::RISE:ot::FALL;
            auto cel=el==ot::MIN?ot::MAX:ot::MIN;
            auto clock=context.smooth_arrivals[test._arc._from.idx()][cel][crf];
            auto data=context.smooth_arrivals[test._arc._to.idx()][el][rf];
            if(constraint && clock && data) {
              double required=el==ot::MIN?*clock+*constraint:*clock+timer._clocks.begin()->second.period()-*constraint;
              slack=el==ot::MIN?*data-required:required-*data;
            }
          }
          endpoint(test.slack(el,rf),slack);
        }
      }
      double ht=0,st=0;
      for(double x:hard_slacks) ht+=std::min(x,0.0);
      for(double x:smooth_slacks) st+=std::min(x,0.0);
      double hw=hard_slacks.empty()?0:*std::min_element(hard_slacks.begin(),hard_slacks.end());
      double sw=smooth_slacks.empty()?0:*std::min_element(smooth_slacks.begin(),smooth_slacks.end());
      if(!smooth_slacks.empty()) {
        double sum=0; for(double x:smooth_slacks) sum+=std::exp((sw-x)/tau);
        sw-=tau*std::log(sum);
      }
      auto percent=[](double a,double b){return std::abs(a)>1e-9?100*std::abs(b-a)/std::abs(a):std::numeric_limits<double>::quiet_NaN();};
      std::cout<<std::setprecision(10)<<design<<','<<tau<<','<<ot::to_string(el)<<','<<arrivals<<','<<max_arrival<<','<<max_percent<<','<<hard_slacks.size()<<','<<max_slack<<','<<ht<<','<<st<<','<<percent(ht,st)<<','<<hw<<','<<sw<<','<<percent(hw,sw)<<','<<nonfinite<<','<<missing<<','<<preserved<<'\n';
    }
  }
}
