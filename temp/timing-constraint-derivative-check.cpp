#include <ot/liberty/timing.hpp>
#include <ot/timer/gradient_context.hpp>
#include <cassert>
#include <cmath>

void close(double a,double b,double eps=1e-9) {
  if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>eps)
    throw std::runtime_error("constraint derivative mismatch");
}
int main() {
  ot::GradientOptions options;
  ot::GradientContext context(options);
  ot::LutTemplate axes;
  axes.variable1=ot::LutVar::RELATED_PIN_TRANSITION;
  axes.variable2=ot::LutVar::CONSTRAINED_PIN_TRANSITION;
  ot::Timing timing;
  timing.type=ot::TimingType::SETUP_RISING;
  ot::Lut table;
  table.lut_template=&axes;
  table.indices1={0,2}; table.indices2={0,4};
  table.table={4,16,8,20}; // 2*clock_slew + 3*data_slew + 4
  timing.rise_constraint=table;
  auto check=[&](float clock,float data) {
    const auto value=timing.constraint(ot::RISE,ot::RISE,clock,data,&context);
    assert(value); close(*value,*timing.constraint(ot::RISE,ot::RISE,clock,data));
    const auto d=context.constraint_derivatives.at(&timing).at({ot::RISE,ot::RISE,clock,data});
    close(d.drelated_slew,2); close(d.dconstrained_slew,3);
    const float h=0.01f;
    close((*timing.constraint(ot::RISE,ot::RISE,clock+h,data)-*timing.constraint(ot::RISE,ot::RISE,clock-h,data))/double((clock+h)-(clock-h)),d.drelated_slew,1e-3);
    close((*timing.constraint(ot::RISE,ot::RISE,clock,data+h)-*timing.constraint(ot::RISE,ot::RISE,clock,data-h))/double((data+h)-(data-h)),d.dconstrained_slew,1e-3);
  };
  check(1,2); check(-1,5);
  axes.variable1=ot::LutVar::CONSTRAINED_PIN_TRANSITION;
  axes.variable2=ot::LutVar::RELATED_PIN_TRANSITION;
  timing.rise_constraint->indices1={0,4}; timing.rise_constraint->indices2={0,2};
  timing.rise_constraint->table={4,8,16,20};
  check(1,2); check(-1,5);
  assert(!timing.constraint(ot::FALL,ot::RISE,1,2,&context));
  assert(!timing.constraint(ot::RISE,ot::FALL,1,2,&context));
  ot::Lut scalar; scalar.indices1={0}; scalar.indices2={0}; scalar.table={7};
  timing.fall_constraint=scalar;
  close(*timing.constraint(ot::RISE,ot::FALL,1,2,&context),7);
  const auto d=context.constraint_derivatives.at(&timing).at({ot::RISE,ot::FALL,1,2});
  close(d.drelated_slew,0); close(d.dconstrained_slew,0); close(d.interpolation.table_weights[0],1);
  timing.type=ot::TimingType::HOLD_FALLING;
  close(*timing.constraint(ot::FALL,ot::FALL,1,2,&context),7);
  assert(!timing.constraint(ot::RISE,ot::FALL,1,2,&context));
  timing.type=ot::TimingType::SETUP_RISING;
  std::vector<std::thread> threads;
  for(int i=0;i<4;++i) threads.emplace_back([&]{for(int j=0;j<100;++j) timing.constraint(ot::RISE,ot::RISE,1,2,&context);});
  for(auto& t:threads) t.join();
  close(context.constraint_derivatives.at(&timing).at({ot::RISE,ot::RISE,1,2}).dconstrained_slew,3);
  std::cout<<"PASS: constraint derivatives, swapped axes, extrapolation, scalar, setup/hold edges, parallel calls\n";
}
