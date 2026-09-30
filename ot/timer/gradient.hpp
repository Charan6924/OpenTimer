#ifndef OT_TIMER_GRADIENT_HPP_
#define OT_TIMER_GRADIENT_HPP_

#include <ot/headerdef.hpp>

namespace ot {

enum class TimingObjective {
  TNS,
  WNS
};

enum class TimingArcKind {
  CELL,
  NET
};

struct GradientOptions {
  TimingObjective objective {TimingObjective::TNS};
  std::optional<Split> split;
  std::optional<Tran> transition;
  double arrival_temperature {0.0};
  double objective_temperature {0.0};
};

struct ArcDelayGradient {
  size_t arc_id {0};
  TimingArcKind kind {TimingArcKind::CELL};
  std::string from_pin;
  std::string to_pin;

  TimingData<
    std::optional<double>,
    MAX_SPLIT,
    MAX_TRAN,
    MAX_TRAN
  > value;
};

struct GradientReport{
    TimingObjective objective {TimingObjective::TNS};
    std::optional<Split> split;
    std::optional<Tran> transition;

    double arrival_temperature {0.0};
    double objective_temperature {0.0};

    std::optional<double> objective_value;

    std::vector<ArcDelayGradient> arcs;
};

}  



#endif
