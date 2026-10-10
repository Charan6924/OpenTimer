#include <ot/liberty/lut.hpp>
#include <ot/timer/gradient_context.hpp>
#include <cassert>
#include <cmath>

void close(double a, double b, double tolerance = 1e-9) {
  if(std::abs(a-b) > tolerance) throw std::runtime_error("LUT derivative mismatch");
}

void check(ot::Lut& lut, float x, float y, double dx, double dy) {
  ot::GradientOptions options;
  ot::GradientContext context(options);
  const float value = lut(x, y, &context);
  close(value, lut(x, y));
  const auto record = context.lut_derivatives.at(&lut).at({x,y});
  close(record.dval1, dx);
  close(record.dval2, dy);
  double sum=0;
  for(size_t k=0;k<record.count;++k) sum+=record.table_weights[k];
  close(sum, 1);
  // Finite differences stay within a selected interval for these test points.
  const float h=0.01f;
  close((lut(x+h,y)-lut(x-h,y))/double((x+h)-(x-h)), dx, 1e-3);
  close((lut(x,y+h)-lut(x,y-h))/double((y+h)-(y-h)), dy, 1e-3);
  for(size_t k=0;k<record.count;++k) {
    const size_t index=record.table_indices[k];
    const float old=lut.table[index];
    lut.table[index]=old+h; const float plus=lut(x,y);
    lut.table[index]=old-h; const float minus=lut(x,y);
    lut.table[index]=old;
    close((plus-minus)/double((old+h)-(old-h)), record.table_weights[k], 1e-3);
  }
}

int main() {
  ot::Lut scalar; scalar.indices1={0}; scalar.indices2={0}; scalar.table={7};
  check(scalar,2,3,0,0);
  ot::Lut row; row.indices1={0}; row.indices2={0,2}; row.table={1,7};
  check(row,0.5f,1,0,3); check(row,0.5f,3,0,3);
  ot::Lut col; col.indices1={0,2}; col.indices2={0}; col.table={1,5};
  check(col,1,0.5f,2,0); check(col,-1,0.5f,2,0);
  ot::Lut plane; plane.indices1={0,2}; plane.indices2={0,4}; plane.table={4,16,8,20};
  check(plane,1,2,2,3); check(plane,-1,5,2,3);
  ot::Lut mixed; mixed.indices1={0,2}; mixed.indices2={0,4}; mixed.table={0,0,0,8};
  check(mixed,1,2,2,1); check(mixed,3,5,5,3);
  ot::Lut knots; knots.indices1={0,1,2}; knots.indices2={0}; knots.table={0,2,8};
  ot::GradientOptions options; ot::GradientContext context(options);
  knots(1,0,&context); close(context.lut_derivatives.at(&knots).at({1,0}).dval1,2);
  std::vector<std::thread> workers;
  for(int i=0;i<4;++i) workers.emplace_back([&]{for(int j=0;j<100;++j) plane(1,2,&context);});
  for(auto& worker:workers) worker.join();
  close(context.lut_derivatives.at(&plane).at({1,2}).dval2,3);
  bool rejected=false;
  try { plane(INFINITY,0,&context); } catch(const std::invalid_argument&) {rejected=true;}
  assert(rejected);
  std::cout << "PASS: scalar, 1D, bilinear, extrapolation, knot, table weights, parallel recording\n";
}
