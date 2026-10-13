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



size_t workers(const tf::Executor& e) { return e.num_workers(); }
size_t workers(const std::unique_ptr<tf::Executor>& e) { return e->num_workers(); }
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
  ot::Timer timer;
  if(argc>1 && std::string(argv[1])=="threads") {
    timer.insert_primary_input("in").set_at("in",ot::MAX,ot::RISE,3.0f);
    for(unsigned n: {1u,4u,2u,1u}) {
      timer.set_num_threads(n);
      require(workers(timer._executor)==n,"requested worker count not applied");
      require(timer.report_at("in",ot::MAX,ot::RISE)==3.0f,"pending builders lost");
    }
    bool rejected=false;
    try { timer.set_num_threads(0); } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"zero workers accepted");
    std::cout<<"PASS: real worker counts, pending builders, zero rejection\n";
    return 0;
  }
  std::ostringstream actual;
  timer.read_celllib("example/simple/osu018_stdcells.lib",ot::MIN)
    .read_celllib("example/simple/osu018_stdcells.lib",ot::MAX);
  timer.insert_primary_input("i").insert_primary_output("o");
  timer.insert_net("i").insert_net("o").insert_gate("g","NAND2X1");
  timer.connect_pin("i","i").connect_pin("g:A","i").connect_pin("g:B","i")
    .connect_pin("g:Y","o").connect_pin("o","o");
  for(auto el:ot::SPLIT) for(auto rf:ot::TRAN)
    timer.set_at("i",el,rf,0.0f).set_slew("i",el,rf,3.0f).set_load("o",el,rf,0.5f);
  timer.update_timing();
  ot::GradientOptions options;
  options.split=ot::MAX; options.arrival_temperature=10; options.objective_temperature=0.1;
  actual<<std::setprecision(17);
  for(int mode=0;mode<4;++mode) {
    for(auto rf:ot::TRAN) timer.set_rat("o",ot::MAX,rf,
      mode==0 ? 100000.f : mode==1 ? -100.f : timer.report_at("o",ot::MAX,rf).value()+0.01f);
    options.objective=mode==3 ? ot::TimingObjective::WNS : ot::TimingObjective::TNS;
    const auto report=timer.report_gradients(options);
    require(bool(report.objective_value),"objective missing");
    if(mode==0) require(*report.objective_value==0,"inactive TNS nonzero");
    if(mode==1 || mode==2) require(*report.objective_value<0,"active smooth TNS expected");
    if(mode==2) require(timer.report_tns(ot::MAX)==0,"hard TNS should be zero");
    actual<<"mode "<<mode<<" objective "<<*report.objective_value<<'\n';
    for(const auto& a:report.arcs) {
      actual<<"arc "<<a.arc_id<<' '<<int(a.kind)<<' '<<a.from_pin<<' '<<a.to_pin;
      for(auto el:ot::SPLIT) for(auto frf:ot::TRAN) for(auto trf:ot::TRAN) {
        const auto v=a.value[el][frf][trf]; actual<<' '<<bool(v)<<' '<<v.value_or(0.0);
        if(mode==0 && v) require(*v==0,"inactive arc gradient nonzero");
      }
      actual<<'\n';
    }
    for(const auto& p:report.pins) {
      actual<<"pin "<<p.pin<<' '<<p.primary_input<<' '<<p.primary_output;
      for(auto el:ot::SPLIT) for(auto rf:ot::TRAN)
        actual<<' '<<p.arrival[el][rf]<<' '<<p.slew[el][rf]<<' '<<p.capacitance[el][rf];
      actual<<'\n';
    }
    for(const auto& lut:report.luts) {
      actual<<"lut "<<lut.table_id<<' '<<std::quoted(lut.name)<<' '<<lut.uses.size();
      for(const auto& use:lut.uses) actual<<' '<<use.arc_id<<' '<<use.split<<' '<<int(use.kind)<<' '<<use.transition;
      for(double v:lut.values) actual<<' '<<v;
      actual<<'\n';
    }
  }
  if(argc>1 && std::string(argv[1])=="dump") {
    std::cout << actual.str();
    return 0;
  }
  const std::string reference_path = argc>1 ? argv[1] : "temp/gradient-performance-reference.txt";
  std::ifstream reference(reference_path);
  require(bool(reference), "cannot open baseline gradient reference");
  std::istringstream observed(actual.str());
  std::string expected_token, actual_token;
  size_t token=0;
  while(reference >> expected_token) {
    ++token;
    require(bool(observed >> actual_token), "gradient report is truncated");
    if(expected_token==actual_token) continue;
    char *expected_end=nullptr, *actual_end=nullptr;
    const double expected=std::strtod(expected_token.c_str(), &expected_end);
    const double value=std::strtod(actual_token.c_str(), &actual_end);
    if(expected_end!=expected_token.c_str() && actual_end!=actual_token.c_str()
       && *expected_end==0 && *actual_end==0 && std::isfinite(expected) && std::isfinite(value)
       && std::abs(expected-value)<=1e-12*(1.0+std::max(std::abs(expected),std::abs(value)))) continue;
    throw std::runtime_error("gradient reference mismatch at token " + std::to_string(token)
      + ": expected " + expected_token + ", got " + actual_token);
  }
  require(!(observed >> actual_token), "gradient report has unexpected entries");
  std::cout << "PASS: full baseline reports, optional arc states, names, LUT uses, zero/active TNS and WNS\n";

}
