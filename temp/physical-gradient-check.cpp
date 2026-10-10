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


void close(double analytic, double numerical, const std::string& label) {
  if(!std::isfinite(analytic) || !std::isfinite(numerical) ||
      std::abs(analytic-numerical)>0.003+0.025*std::max(std::abs(analytic),std::abs(numerical))) {
    std::cerr<<label<<": analytic="<<analytic<<" numerical="<<numerical<<'\n';
    throw std::runtime_error("finite difference mismatch");
  }
}
void check_simple() {
  ot::Timer timer;
  timer.read_celllib("example/simple/osu018_stdcells.lib",ot::MIN)
    .read_celllib("example/simple/osu018_stdcells.lib",ot::MAX)
    .read_verilog("example/simple/simple.v").read_sdc("example/simple/simple.sdc");
  ot::GradientOptions options;
  options.arrival_temperature=0.1; options.objective_temperature=0.1;
  options.split=ot::MAX;
  auto run=[&]{return timer.report_gradients(options);};
  for(auto objective:{ot::TimingObjective::TNS,ot::TimingObjective::WNS}) {
    options.objective=objective;
    auto report=run();
    if(report.pins.empty() || report.luts.empty()) throw std::runtime_error("missing physical report");
    for(const auto& pin:report.pins) {
      if(pin.pin!="inp1" && pin.pin!="out") continue;
      for(auto rf:ot::TRAN) {
        const bool input=pin.pin=="inp1";
        const double original=input?timer._pis.at("inp1")._slew[ot::MAX][rf].value():timer._pos.at("out")._load[ot::MAX][rf];
        const double h=0.01;
        auto perturb=[&](double v){if(input) timer.set_slew(pin.pin,ot::MAX,rf,v); else timer.set_load(pin.pin,ot::MAX,rf,v);};
        perturb(original+h); double plus=*run().objective_value;
        perturb(original-h); double minus=*run().objective_value;
        perturb(original);
        close(input?pin.slew[ot::MAX][rf]:pin.capacitance[ot::MAX][rf],(plus-minus)/(2*h),pin.pin+" input/load");
      }
    }
  }
  std::cout<<"PASS: physical report, input slew and output load finite differences\n";
}

void check_models() {
  ot::Timer timer;
  timer.read_celllib("example/simple/osu018_stdcells.lib",ot::MIN)
    .read_celllib("example/simple/osu018_stdcells.lib",ot::MAX);
  timer.insert_primary_input("i").insert_primary_output("o");
  for(const auto* name:{"i","middle","o"}) timer.insert_net(name);
  timer.insert_gate("g1","INVX1").insert_gate("g2","INVX1");
  timer.connect_pin("i","i").connect_pin("g1:A","i")
    .connect_pin("g1:Y","middle").connect_pin("g2:A","middle")
    .connect_pin("g2:Y","o").connect_pin("o","o");
  for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) {
    timer.set_at("i",el,rf,1.0f).set_slew("i",el,rf,3.0f)
      .set_load("o",el,rf,0.5f).set_rat("o",el,rf,el==ot::MIN?100.0f:-100.0f);
  }
  timer.update_timing();
  auto& net=timer._nets.at("middle");
  auto& tree=net._rct.emplace<ot::Rct>();
  tree.insert_node("g1:Y",0.2f); tree.insert_node("junction",0.3f); tree.insert_node("g2:A",0.4f); tree.insert_node("branch",0.25f);
  tree.insert_segment("g1:Y","junction",0.03f); tree.insert_segment("junction","g2:A",0.02f); tree.insert_segment("junction","branch",0.04f);
  auto invalidate=[&]{net._invalidate_rc_timing();};
  invalidate();
  ot::GradientOptions options;
  options.arrival_temperature=0.1;options.objective_temperature=0.1;
  auto objective=[&]{return *timer.report_gradients(options).objective_value;};
  size_t checked=0;
  for(auto kind:{ot::TimingObjective::TNS,ot::TimingObjective::WNS})
  for(auto split:ot::SPLIT) for(auto transition:ot::TRAN) {
    options.objective=kind;options.split=split;options.transition=transition;
    auto report=timer.report_gradients(options);
    for(const auto& res:report.rc_resistances) {
      if(res.net!="middle") continue;
      std::vector<ot::RctEdge*> edges;
      for(auto& edge:tree._edges) if((edge._from._name==res.from_node && edge._to._name==res.to_node)||
         (edge._from._name==res.to_node && edge._to._name==res.from_node)) edges.push_back(&edge);
      const float original=edges.at(0)->_res; const double h=0.0005;
      for(auto* edge:edges) edge->_res=original+h; invalidate(); double plus=objective();
      for(auto* edge:edges) edge->_res=original-h; invalidate(); double minus=objective();
      for(auto* edge:edges) edge->_res=original; invalidate();
      close(res.value,(plus-minus)/(2*h),"RC resistance "+res.from_node);++checked;
    }
    for(const auto& cap:report.rc_capacitances) {
      if(cap.net!="middle") continue;
      auto* node=tree._node(cap.node);const auto original=node->_ncap;const double h=0.01;
      for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) node->_ncap[el][rf]=original[el][rf]+h;
      invalidate(); double plus=objective();
      for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) node->_ncap[el][rf]=original[el][rf]-h;
      invalidate(); double minus=objective();
      node->_ncap=original;invalidate();
      close(cap.value,(plus-minus)/(2*h),"RC capacitance "+cap.node);++checked;
    }

    // A shared generic Liberty capacitance affects every matching instance
    // and each transition that does not override it with a dedicated value.
    auto* cp=const_cast<ot::Cellpin*>(timer._pins.at("g2:A").cellpin(split));
    if(cp->capacitance) {
      double expected=0;
      for(const auto& pin:report.pins) {
        if(timer._pins.at(pin.pin).cellpin(split)!=cp) continue;
        if(!cp->rise_capacitance) expected+=pin.capacitance[split][ot::RISE];
        if(!cp->fall_capacitance) expected+=pin.capacitance[split][ot::FALL];
      }
      const float original=*cp->capacitance;const double h=0.01;
      auto invalidate_all=[&]{for(auto& entry:timer._nets) entry.second._invalidate_rc_timing();};
      cp->capacitance=original+h;invalidate_all();double plus=objective();
      cp->capacitance=original-h;invalidate_all();double minus=objective();
      cp->capacitance=original;invalidate_all();
      close(expected,(plus-minus)/(2*h),"shared instance pin capacitance");++checked;
    }
    ot::GradientContext context(options);timer._update_timing(&context);
    size_t tables=0;
    for(const auto& entry:context.lut_adjoints) {
      const auto& values=entry.second;
      auto it=std::max_element(values.begin(),values.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
      if(it==values.end() || std::abs(*it)<1e-7) continue;
      auto* lut=const_cast<ot::Lut*>(entry.first);const size_t index=it-values.begin();
      const float original=lut->table[index];const double h=0.001;
      lut->table[index]=original+h;double plus=objective();
      lut->table[index]=original-h;double minus=objective();
      lut->table[index]=original;
      close(*it,(plus-minus)/(2*h),"shared LUT entry");++checked;++tables;
    }
    if(!tables) throw std::runtime_error("no nonzero LUT gradients tested");
  }
  std::cout<<"PASS: "<<checked<<" RC/LUT finite differences, both objectives and all split/transition filters\n";
}

void check_constraints() {
  ot::Timer timer;
  timer.insert_primary_input("clock").insert_primary_input("data");
  timer.create_clock("clock","clock",10.0f);
  for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) {
    timer.set_at("clock",el,rf,0.0f).set_slew("clock",el,rf,2.0f);
    timer.set_at("data",el,rf,el==ot::MIN?0.0f:30.0f).set_slew("data",el,rf,3.0f);
  }
  timer.update_timing();
  ot::LutTemplate axes;
  axes.variable1=ot::LutVar::RELATED_PIN_TRANSITION;
  axes.variable2=ot::LutVar::CONSTRAINED_PIN_TRANSITION;
  ot::Lut table;
  table.lut_template=&axes;table.indices1={0,4};table.indices2={0,4};
  table.table={4,16,12,24}; // 2 * clock slew + 3 * data slew + 4
  ot::Timing hold,setup;
  hold.type=ot::TimingType::HOLD_RISING;setup.type=ot::TimingType::SETUP_RISING;
  hold.rise_constraint=hold.fall_constraint=setup.rise_constraint=setup.fall_constraint=table;
  auto& arc=timer._insert_arc(timer._pins.at("clock"),timer._pins.at("data"),ot::TimingView{&hold,&setup});
  timer._insert_test(arc);
  ot::GradientOptions options;
  options.arrival_temperature=0.1;options.objective_temperature=0.1;
  size_t checked=0;
  for(auto kind:{ot::TimingObjective::TNS,ot::TimingObjective::WNS})
  for(auto split:ot::SPLIT) for(auto rf:ot::TRAN) {
    options.objective=kind;options.split=split;options.transition=rf;
    auto report=timer.report_gradients(options);
    if(!report.objective_value) throw std::runtime_error("missing setup/hold objective");
    auto cel=split==ot::MIN?ot::MAX:ot::MIN;
    for(const auto& pin:report.pins) {
      const bool clock=pin.pin=="clock";
      const auto el=clock?cel:split;const auto tr=clock?ot::RISE:rf;
      close(pin.slew[el][tr],clock?-2.0:-3.0,"constraint analytic slew seed");
      const float original=timer._pis.at(pin.pin)._slew[el][tr].value();const double h=0.01;
      timer.set_slew(pin.pin,el,tr,original+h);double plus=*timer.report_gradients(options).objective_value;
      timer.set_slew(pin.pin,el,tr,original-h);double minus=*timer.report_gradients(options).objective_value;
      timer.set_slew(pin.pin,el,tr,original);
      close(pin.slew[el][tr],(plus-minus)/(2*h),"constraint clock/data slew");++checked;
      const float at=timer._pis.at(pin.pin)._at[el][tr].value();
      timer.set_at(pin.pin,el,tr,at+h);plus=*timer.report_gradients(options).objective_value;
      timer.set_at(pin.pin,el,tr,at-h);minus=*timer.report_gradients(options).objective_value;
      timer.set_at(pin.pin,el,tr,at);
      close(pin.arrival[el][tr],(plus-minus)/(2*h),"constraint input arrival");++checked;
    }
    ot::GradientContext context(options);timer._update_timing(&context);
    for(const auto& entry:context.lut_adjoints) {
      if(std::all_of(entry.second.begin(),entry.second.end(),[](double v){return v==0;})) continue;
      auto* lut=const_cast<ot::Lut*>(entry.first);const float original=lut->table[0];const double h=0.01;
      lut->table[0]=original+h;double plus=*timer.report_gradients(options).objective_value;
      lut->table[0]=original-h;double minus=*timer.report_gradients(options).objective_value;
      lut->table[0]=original;
      close(entry.second[0],(plus-minus)/(2*h),"constraint LUT entry");++checked;
    }
  }
  std::cout<<"PASS: "<<checked<<" setup/hold clock/data slew, arrival and constraint LUT finite differences\n";
}

void check_delay_without_slew() {
  ot::Timer timer;
  timer.insert_primary_input("i").insert_primary_output("o");
  timer.disconnect_pin("o").connect_pin("o","i");
  for(auto el:ot::SPLIT) for(auto rf:ot::TRAN) {
    timer.set_at("i",el,rf,0.0f).set_rat("o",el,rf,el==ot::MIN?10.0f:-10.0f).set_load("o",el,rf,2.0f);
  }
  timer.update_timing();
  auto& net=timer._nets.at("i");
  auto& tree=net._rct.emplace<ot::Rct>();
  tree.insert_node("i",0.0f);tree.insert_node("o",1.0f);tree.insert_segment("i","o",0.5f);
  net._invalidate_rc_timing();
  ot::GradientOptions options;options.arrival_temperature=options.objective_temperature=0.1;
  options.split=ot::MAX;options.transition=ot::RISE;
  auto report=timer.report_gradients(options);
  if(!report.objective_value || report.rc_resistances.empty()) throw std::runtime_error("missing direct net delay gradient");
  close(report.rc_resistances.front().value,-3.0,"net delay without asserted slew");
  std::cout<<"PASS: delay-only net needs no slew derivative\n";
}
int main(){check_simple();check_models();check_constraints();check_delay_without_slew();}
