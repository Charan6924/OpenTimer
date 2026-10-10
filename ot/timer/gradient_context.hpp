#ifndef OT_TIMER_GRADIENT_CONTEXT_HPP_
#define OT_TIMER_GRADIENT_CONTEXT_HPP_

#include <ot/timer/gradient.hpp>

namespace ot {

struct Lut;
struct Timing;
class Rct;
class RctNode;
class RctEdge;
class Pin;
class Arc;
class Test;
class Net;

// Struct to store the derivative of pin's smooth min/max slew with respect to this candidate slew
struct SlewCandidateWeight {
  const Arc* arc {nullptr};
  Split source_split {MIN};
  Tran source_transition {RISE};
  double weight {0.0}; // the local derivative
};

// Slew calculated from multiple candidates using soft min/max
struct SmoothSlewState {
  std::optional<double> value;
  std::vector<SlewCandidateWeight> candidates;
};

// Different types of RC tree ops
enum class RcOperation {
  LOAD,
  DELAY,
  LOAD_DELAY,
  RESPONSE,
  SLEW
};

// Turn rc tree into a graph (root has no parent)
struct RcNodeRelationships {
  const RctNode* parent {nullptr};
  const RctEdge* parent_edge {nullptr}; // edge to parent
  const Pin* pin {nullptr};
  std::vector<const RctNode*> children;
};

// Derivatives for one node and one split/transition state.
struct RcLocalDerivatives {
  // Load = local capacitance + sum of child loads.
  double dload_dcap {1.0};
  double dload_dchild_load {1.0};

  // Delay = parent delay + parent resistance * load.
  double ddelay_dparent_delay {1.0};
  double ddelay_dresistance {0.0};
  double ddelay_dload {0.0};

  // Load delay = local capacitance * delay + sum of child load delays.
  double dload_delay_dcap {0.0};
  double dload_delay_ddelay {0.0};
  double dload_delay_dchild_load_delay {1.0};

  // Beta = parent beta + parent resistance * load delay.
  double dbeta_dparent_beta {1.0};
  double dbeta_dresistance {0.0};
  double dbeta_dload_delay {0.0};

  // Impulse = 2 * beta - delay squared.
  double dimpulse_dbeta {2.0};
  double dimpulse_ddelay {0.0};

  // make sure derivates are finite
  void validate() const {
    for(double value : {dload_dcap, dload_dchild_load, ddelay_dparent_delay,
        ddelay_dresistance, ddelay_dload, dload_delay_dcap, dload_delay_ddelay,
        dload_delay_dchild_load_delay, dbeta_dparent_beta, dbeta_dresistance,
        dbeta_dload_delay, dimpulse_dbeta, dimpulse_ddelay}) {
      if(!std::isfinite(value)) {
        throw std::domain_error("RC local derivative must be finite");
      }
    }
  }
};

// Derivative of output wrt to impulse and input slew
struct RcSlewDerivatives {
  double dinput_slew {0.0};
  double dimpulse {0.0};
};

// Local derivatives
struct RcNodeAdjoints {
  double load {0.0};
  double delay {0.0};
  double load_delay {0.0};
  double beta {0.0};
  double impulse {0.0};
  double wire_capacitance {0.0};
  double pin_capacitance {0.0};
};

// Whole tree
struct RcTreeGradientData {
  std::unordered_map<const RctNode*,
    TimingData<RcNodeAdjoints, MAX_SPLIT, MAX_TRAN>> adjoints;
  // Use only edges oriented from parent to child in this rooted tree.
  std::unordered_map<const RctEdge*,
    TimingData<double, MAX_SPLIT, MAX_TRAN>> resistance_adjoints;
};


// Cache derivatives when RC values are cached too
struct RcDerivativeCache{
  std::vector<const RctNode*> traversal;
  std::unordered_map<const RctNode*, RcNodeRelationships> nodes;
  std::unordered_map<const RctNode*, TimingData<RcLocalDerivatives, MAX_SPLIT, MAX_TRAN>> local_derivatives;
};

// LUT interpolation derivatives
struct LutDerivatives {
  double value {0.0};
  double dval1 {0.0};
  double dval2 {0.0};
  std::array<size_t, 4> table_indices {};
  std::array<double, 4> table_weights {};
  size_t count {0};
};

// Derivative of cell delay wrt lut
struct CellDelayDerivatives {
  const Lut* lut {nullptr};
  double dinput_slew {0.0}; // derivative wrt to inptu slew
  double dload {0.0}; // derivative wrt to output load
  LutDerivatives interpolation;
};

// Cell delay and transition tables have the same physical lookup inputs
using CellSlewDerivatives = CellDelayDerivatives;


struct ConstraintDerivatives {
  const Lut* lut {nullptr};
  double drelated_slew {0.0};
  double dconstrained_slew {0.0};
  LutDerivatives interpolation;
};

using GradientPinData = TimingData<std::optional<double>, MAX_SPLIT, MAX_TRAN>;
using GradientArcData = TimingData<std::optional<double>, MAX_SPLIT, MAX_TRAN, MAX_TRAN>;

// Store forward pass values and local derivatives here
struct GradientContext {
  explicit GradientContext(const GradientOptions& o) : options {o} {}

  static double require_finite(double value, const char* description) {
    if(!std::isfinite(value)) {
      throw std::domain_error(description);
    }
    return value;
  }

  const GradientOptions& options;
  std::vector<GradientPinData> smooth_arrivals;
  std::vector<GradientPinData> pin_adjoints;
  std::vector<GradientArcData> reduction_weights;
  std::vector<GradientArcData> arc_gradients;
  std::vector<GradientPinData> slew_adjoints;
  std::vector<GradientArcData> arc_slew_contributions;
  std::vector<GradientArcData> arc_load_contributions;
  std::vector<GradientArcData> arc_impulse_contributions;
  std::unordered_map<const Test*, GradientPinData> constraint_adjoints;
  std::unordered_map<const Lut*, std::vector<double>> lut_adjoints;
  std::mutex model_adjoints_mutex;
  std::optional<double> objective_value;
  GradientReport report;
  std::unordered_map<const Pin*, TimingData<SmoothSlewState, MAX_SPLIT, MAX_TRAN>> smooth_slews;
  std::mutex slew_derivatives_mutex;
  std::unordered_map<const Arc*, GradientArcData> smooth_delays;
  std::unordered_map<const Test*, GradientPinData> smooth_constraints;
  std::mutex smooth_values_mutex;

  std::optional<float> smooth_slew(const Pin* pin, Split el, Tran rf, bool required = false) {
    std::scoped_lock lock(slew_derivatives_mutex);
    const auto found = smooth_slews.find(pin);
    if(found == smooth_slews.end() || !found->second[el][rf].value) {
      if(required) throw std::runtime_error("missing smooth slew for a timed pin state");
      return std::nullopt;
    }
    const float value = static_cast<float>(*found->second[el][rf].value);
    require_finite(value, "smooth slew is nonfinite or outside float range");
    return value;
  }

  std::unordered_map<const Rct*, RcTreeGradientData> rc_trees;
  using RcSlewKey = std::tuple<Split, Tran, float>;
  std::unordered_map<const RctNode*,
    std::map<RcSlewKey, RcSlewDerivatives>> rc_slew_derivatives;
  std::mutex rc_derivatives_mutex;

  std::unordered_map<const Lut*, std::map<std::pair<float, float>, LutDerivatives>> lut_derivatives;
  using CellDelayKey = std::tuple<Tran, Tran, float, float>;
  std::unordered_map<const Timing*, std::map<CellDelayKey, CellDelayDerivatives>> cell_delay_derivatives;
  using CellSlewKey = CellDelayKey;
  std::unordered_map<const Timing*, std::map<CellSlewKey, CellSlewDerivatives>> cell_slew_derivatives;
  using ConstraintKey = std::tuple<Tran, Tran, float, float>;
  std::unordered_map<const Timing*, std::map<ConstraintKey, ConstraintDerivatives>> constraint_derivatives;
  std::mutex lut_derivatives_mutex;
};

}  // namespace ot

#endif
