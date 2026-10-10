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
  bool collect_lut_gradients {true};
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

// Derivatives use the timer's internal units: objective units / parameter units.
struct PinParameterGradient {
  std::string pin;
  bool primary_input {false};
  bool primary_output {false};
  // Arrival/slew refer to supplied primary-input values; zero for other pins.
  TimingData<double, MAX_SPLIT, MAX_TRAN> arrival {};
  TimingData<double, MAX_SPLIT, MAX_TRAN> slew {};
  TimingData<double, MAX_SPLIT, MAX_TRAN> capacitance {};
};

struct RcResistanceGradient {
  std::string net;
  std::string from_node;
  std::string to_node;
  double value {0.0};
};

struct RcCapacitanceGradient {
  std::string net;
  std::string node;
  double value {0.0};
};

enum class LutTimingKind {DELAY, SLEW, CONSTRAINT};
struct LutGradientUse {
  size_t arc_id {0};
  Split split {MIN};
  LutTimingKind kind {LutTimingKind::DELAY};
  Tran transition {RISE};
};
struct LutEntryGradient {
  size_t table_id {0};
  std::string name;
  std::vector<LutGradientUse> uses;
  // Flattened in the same order as Lut::table; shared uses are summed.
  std::vector<double> values;
};

struct GradientReport{
    TimingObjective objective {TimingObjective::TNS};
    std::optional<Split> split;
    std::optional<Tran> transition;

    double arrival_temperature {0.0};
    double objective_temperature {0.0};

    std::optional<double> objective_value;

    std::vector<ArcDelayGradient> arcs;
    std::vector<PinParameterGradient> pins;
    std::vector<RcResistanceGradient> rc_resistances;
    std::vector<RcCapacitanceGradient> rc_capacitances;
    std::vector<LutEntryGradient> luts;
};

}  



#endif
