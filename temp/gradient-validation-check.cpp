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


template <typename F> void rejects(F f) {
  bool threw=false;
  try { f(); } catch(const std::exception&) { threw=true; }
  if(!threw) throw std::runtime_error("expected validation error");
}
int main() {
  ot::GradientOptions options;
  options.arrival_temperature=0.01;
  options.objective_temperature=0.01;
  ot::GradientContext context(options);
  ot::RctNode node("node");
  node._impulse[ot::MAX][ot::RISE]=3.0f;
  node.slew(ot::MAX,ot::RISE,1.0f,&context);
  auto d=context.rc_slew_derivatives.at(&node).at({ot::MAX,ot::RISE,1.0f});
  assert(d.dinput_slew==0.5 && d.dimpulse==0.25);
  node.slew(ot::MAX,ot::RISE,-1.0f,&context);
  d=context.rc_slew_derivatives.at(&node).at({ot::MAX,ot::RISE,-1.0f});
  assert(d.dinput_slew==0.5 && d.dimpulse==-0.25);
  rejects([&]{node.slew(ot::MAX,ot::RISE,0.0f,&context);});
  node._impulse[ot::MAX][ot::RISE]=0.0f;
  rejects([&]{node.slew(ot::MAX,ot::RISE,0.0f,&context);});
  node._impulse[ot::MAX][ot::RISE]=-2.0f;
  rejects([&]{node.slew(ot::MAX,ot::RISE,1.0f,&context);});
  rejects([&]{node.slew(ot::MAX,ot::RISE,std::numeric_limits<float>::infinity(),&context);});
  rejects([&]{node.slew(ot::MAX,ot::RISE,std::numeric_limits<float>::quiet_NaN(),&context);});
  assert(!context.smooth_slew(nullptr,ot::MAX,ot::RISE));
  rejects([&]{context.smooth_slew(nullptr,ot::MAX,ot::RISE,true);});
  assert(ot::GradientContext::require_finite(0.0,"zero is valid")==0.0);
  ot::Timer timer;
  timer.insert_primary_input("input");
  timer.set_at("input",ot::MAX,ot::RISE,0.0f);
  timer.set_slew("input",ot::MAX,ot::RISE,std::numeric_limits<float>::quiet_NaN());
  rejects([&]{timer.report_gradients(options);});
  timer.set_slew("input",ot::MAX,ot::RISE,1.0f);
  timer.update_timing();
  timer.report_gradients(options);
  std::cout<<"PASS: finite derivatives, singular/nonfinite rejection, missing-record checks, zero gradients, Taskflow error propagation and recovery\n";
}
