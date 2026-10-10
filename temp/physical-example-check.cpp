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

  auto finite=[](double x){if(!std::isfinite(x)) throw std::runtime_error("nonfinite physical report");};
  auto compare=[&](const ot::GradientReport& a,const ot::GradientReport& b) {
    if(a.objective_value!=b.objective_value || a.pins.size()!=b.pins.size() || a.luts.size()!=b.luts.size() ||
       a.rc_resistances.size()!=b.rc_resistances.size() || a.rc_capacitances.size()!=b.rc_capacitances.size())
      throw std::runtime_error("report shapes/objectives differ");
    auto close=[](double x,double y){if(std::abs(x-y)>1e-9*(1+std::max(std::abs(x),std::abs(y)))) throw std::runtime_error("serial/parallel gradient mismatch");};
    for(size_t i=0;i<a.pins.size();++i) for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) {
      close(a.pins[i].arrival[el][rf],b.pins[i].arrival[el][rf]);
      close(a.pins[i].slew[el][rf],b.pins[i].slew[el][rf]);
      close(a.pins[i].capacitance[el][rf],b.pins[i].capacitance[el][rf]);
    }
    for(size_t i=0;i<a.rc_resistances.size();++i) close(a.rc_resistances[i].value,b.rc_resistances[i].value);
    for(size_t i=0;i<a.rc_capacitances.size();++i) close(a.rc_capacitances[i].value,b.rc_capacitances[i].value);
    for(size_t i=0;i<a.luts.size();++i) for(size_t j=0;j<a.luts[i].values.size();++j) close(a.luts[i].values[j],b.luts[i].values[j]);
  };
  std::vector<ot::GradientReport> parallel;
  size_t queries=0;
  for(int mode=0;mode<2;++mode) {
    if(mode) {timer._executor.~Executor();new(&timer._executor) tf::Executor(1);}
    size_t index=0;
    for(double tau:{0.1,0.001,0.000001})
    for(auto objective:{ot::TimingObjective::TNS,ot::TimingObjective::WNS})
    for(auto split:{std::optional<ot::Split>{},std::optional<ot::Split>{ot::MIN},std::optional<ot::Split>{ot::MAX}})
    for(auto transition:{std::optional<ot::Tran>{},std::optional<ot::Tran>{ot::RISE},std::optional<ot::Tran>{ot::FALL}}) {
      ot::GradientOptions options;options.arrival_temperature=tau;options.objective_temperature=tau;
      options.objective=objective;options.split=split;options.transition=transition;
      auto report=timer.report_gradients(options);
      if(report.objective_value) finite(*report.objective_value);
      if(report.pins.size()!=timer._pins.size()) throw std::runtime_error("missing pin parameters");
      for(const auto& pin:report.pins) for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) {
        finite(pin.arrival[el][rf]);finite(pin.slew[el][rf]);finite(pin.capacitance[el][rf]);
      }
      for(const auto& x:report.rc_resistances) finite(x.value);
      for(const auto& x:report.rc_capacitances) finite(x.value);
      for(const auto& x:report.luts) for(double y:x.values) finite(y);
      if(mode) compare(parallel.at(index),report); else parallel.push_back(report);
      ++index;++queries;
    }
  }
  std::ostringstream after,after_rat;timer.dump_at(after);timer.dump_rat(after_rat);
  if(after.str()!=before.str() || after_rat.str()!=before_rat.str()) throw std::runtime_error("exact timing changed");
  ot::GradientOptions options;options.arrival_temperature=options.objective_temperature=0.1;
  auto full=timer.report_gradients(options);
  options.collect_lut_gradients=false;
  auto no_lut=timer.report_gradients(options);
  if(!no_lut.luts.empty() || no_lut.objective_value!=full.objective_value) throw std::runtime_error("LUT option failed");
  auto expected_without_luts=full;
  expected_without_luts.luts.clear();
  compare(expected_without_luts,no_lut);
  for(size_t i=0;i<full.arcs.size();++i) if(full.arcs[i].value!=no_lut.arcs[i].value) throw std::runtime_error("arc partials changed");
  std::cout<<"PASS "<<design<<": "<<queries<<" queries; serial/parallel, filters, temperatures, finite reports, exact preservation, options\n";
}
