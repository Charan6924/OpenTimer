#include <ot/liberty/timing.hpp>
#include <ot/timer/gradient_context.hpp>
#include <cassert>
#include <cmath>

void close(double a,double b,double eps=1e-9) {
  if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>eps)
    throw std::runtime_error("cell delay derivative mismatch");
}
int main() {
  ot::GradientOptions options;
  ot::GradientContext context(options);
  ot::LutTemplate axes;
  axes.variable1=ot::LutVar::INPUT_NET_TRANSITION;
  axes.variable2=ot::LutVar::TOTAL_OUTPUT_NET_CAPACITANCE;
  ot::Timing timing;
  timing.sense=ot::TimingSense::POSITIVE_UNATE;
  timing.type=ot::TimingType::COMBINATIONAL;
  ot::Lut table;
  table.lut_template=&axes;
  table.indices1={0,2}; table.indices2={0,4};
  table.table={4,16,8,20}; // 2*slew + 3*load + 4
  timing.cell_rise=table;
  auto check=[&](float s,float c) {
    const auto value=timing.delay(ot::RISE,ot::RISE,s,c,&context);
    assert(value); close(*value,*timing.delay(ot::RISE,ot::RISE,s,c));
    const auto d=context.cell_delay_derivatives.at(&timing).at({ot::RISE,ot::RISE,s,c});
    close(d.dinput_slew,2); close(d.dload,3);
    const float h=0.01f;
    close((*timing.delay(ot::RISE,ot::RISE,s+h,c)-*timing.delay(ot::RISE,ot::RISE,s-h,c))/double((s+h)-(s-h)),d.dinput_slew,1e-3);
    close((*timing.delay(ot::RISE,ot::RISE,s,c+h)-*timing.delay(ot::RISE,ot::RISE,s,c-h))/double((c+h)-(c-h)),d.dload,1e-3);
  };
  check(1,2); check(-1,5);
  axes.variable1=ot::LutVar::TOTAL_OUTPUT_NET_CAPACITANCE;
  axes.variable2=ot::LutVar::INPUT_NET_TRANSITION;
  timing.cell_rise->indices1={0,4}; timing.cell_rise->indices2={0,2};
  timing.cell_rise->table={4,8,16,20};
  check(1,2); check(-1,5);
  assert(!timing.delay(ot::RISE,ot::FALL,1,2,&context));
  assert(!timing.delay(ot::FALL,ot::FALL,1,2,&context));
  ot::Lut scalar; scalar.indices1={0}; scalar.indices2={0}; scalar.table={7};
  timing.cell_fall=scalar;
  close(*timing.delay(ot::FALL,ot::FALL,1,2,&context),7);
  const auto d=context.cell_delay_derivatives.at(&timing).at({ot::FALL,ot::FALL,1,2});
  close(d.dinput_slew,0); close(d.dload,0); close(d.interpolation.table_weights[0],1);
  std::vector<std::thread> threads;
  for(int i=0;i<4;++i) threads.emplace_back([&]{for(int j=0;j<100;++j) timing.delay(ot::RISE,ot::RISE,1,2,&context);});
  for(auto& t:threads) t.join();
  close(context.cell_delay_derivatives.at(&timing).at({ot::RISE,ot::RISE,1,2}).dload,3);
  std::cout<<"PASS: cell-delay derivatives, swapped axes, extrapolation, scalar, absent transitions, parallel calls\n";
}
