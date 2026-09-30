# Separate-pass TNS/WNS gradients for OpenTimer v2

Date: 2026-09-28
Status: original design proposal, superseded by the integrated Taskflow implementation

Implementation note (2026-09-30): `report_gradients` now uses the existing propagation
Taskflow with separate smooth-arrival storage, an objective barrier, and reverse
gradient tasks. Exact arrivals and required times are maintained alongside it.
TNS uses `sum(min(slack, 0))`; WNS uses a temperature-scaled soft minimum.
The remaining text records the original separate-pass proposal.

## Purpose and agreed scope

Extend OpenTimer with a gradient query that measures how a small change in every cell-arc or net-arc delay affects a selected TNS or WNS objective. Support MIN/MAX and RISE/FALL objective filters, including omitted filters. Use smooth min/max reductions and reverse-mode automatic differentiation scheduled through Taskflow.

The selected architecture runs a separate smooth arrival pass when gradients are requested. The existing timing update supplies the circuit, computed arc delays, slews, and timing constraints. Pin-arrival adjoints are transient internal state used to propagate derivatives; the public results are delay gradients for cell and net arcs. Incremental gradient updates and CPPR differentiation are future extensions.

The scores are timing sensitivities. They do not partition the existing TNS into additive contributions assigned to arcs. Several arcs can influence the same endpoint violation.

## Mathematical meaning of an arc-delay gradient

An OpenTimer arc has delay states indexed by analysis split, input transition, and output transition. Define an additive delay perturbation for each valid stored delay:

```text
effective_delay[a, el, frf, trf]
    = stored_delay[a, el, frf, trf] + delta_delay[a, el, frf, trf]
```

All perturbations are zero during a normal query. Return:

```text
delay_gradient[a, el, frf, trf]
    = d smooth_objective / d delta_delay[a, el, frf, trf]
```

This measures the first-order sensitivity of the smooth objective to the computed delay of one cell or net arc. It is directly useful for ranking timing arcs, but it is not yet a derivative with respect to gate size, capacitance, resistance, or placement. Those parameters affect delays through additional models that are held fixed in the first version.

Cell-arc gradients exist for every valid `[el][frf][trf]` entry produced from the Liberty timing table. Net-arc gradients normally exist only for transition-preserving `[el][rf][rf]` entries produced from the RC model. Undefined delay states have no gradient value.

Hold the following fixed during differentiation:

- Circuit connectivity, cell selection, legal transitions, and loop-breaker decisions.
- Input slews, RC parameters, pin loads, calculated slews, and the equations used to compute arc delays. The resulting `Arc::_delay` values are the differentiable leaf values.
- Library setup/hold constraints evaluated using those slews.
- Clock period and primary-output required-time constraints.

Capture-clock arrival times are variables and must participate in the derivative calculation.

## Relevant existing implementation

Paths below are relative to the repository root.

| Code | Current behavior relevant to this design |
| --- | --- |
| `ot/timer/timer.cpp`, `_update_timing()` | Executes builders, constructs propagation tasks, executes them, then clears Taskflow and task handles. Returns immediately when there is no lineage. |
| `ot/timer/timer.cpp`, `_build_prop_tasks()` | Creates per-pin forward tasks and backward RAT tasks, with dependencies derived from timing arcs. |
| `ot/timer/arc.cpp`, `_fprop_at()` | Computes a candidate as source arrival plus stored arc delay for each valid transition pair. |
| `ot/timer/pin.cpp`, `_relax_at()` | Selects the smallest MIN arrival or largest MAX arrival, recording a winning arc. |
| `ot/timer/test.cpp`, `_fprop_rat()` | Derives hold/setup required times from capture-clock arrival, clock period, and library constraint. |
| `ot/timer/test.cpp`, `Test::slack()` | Uses data-pin arrival, the test's required time, and optional CPPR credit. |
| `ot/timer/pin.cpp`, `PrimaryOutput::slack()` | Uses output-pin arrival and the primary output's required-time constraint. |
| `ot/timer/timer.cpp`, `_update_endpoints()` | Builds eligible endpoint states and aggregates negative slack and worst slack. |

Endpoint TNS/WNS does not depend on the internal pin RAT values produced by `Timer::_bprop_rat()`. The new gradient calculation therefore does not differentiate that RAT pass. It does differentiate the test required-time equations, which currently execute during the forward pass.

## Proposed public API

The names and structures below describe the intended interface; they are not existing declarations.

```cpp
enum class TimingObjective { TNS, WNS };
enum class TimingArcKind { CELL, NET };

struct GradientOptions {
  TimingObjective objective;
  std::optional<Split> split;
  std::optional<Tran> transition;
  double arrival_temperature;
  double objective_temperature;
};

struct ArcDelayGradient {
  TimingArcKind kind;
  std::string from_pin;
  std::string to_pin;
  size_t arc_id;
  TimingData<std::optional<double>, MAX_SPLIT, MAX_TRAN, MAX_TRAN> value;
};

GradientReport Timer::report_gradients(const GradientOptions&);
```

Temperatures are required, finite, strictly positive values in the timer's current time unit. No numerical default is assumed to be appropriate for every library. Use `double` for smooth arithmetic and gradient accumulation, converting the existing float constants at the boundary.

`GradientReport` contains:

- The objective, filters, temperatures, and time-unit scale used for the query.
- The smooth objective value, optional when no eligible endpoint states exist.
- A collection of `ArcDelayGradient` records. Each record contains a stable query-time arc ID, arc kind, endpoint pin names, and optional values indexed by split, input transition, and output transition.

A valid but uninfluential arc-delay state has gradient zero. An undefined delay state has no gradient value. Include every timing arc in the result so zero sensitivity is distinguishable from missing timing. Return owned result data rather than pointers whose validity depends on later edits. `arc_id` distinguishes parallel arcs within the report snapshot; it is not promised to survive topology edits or identify an arc in a later query.

Filters select objective endpoint states. They do not filter the states through which gradients may propagate. For example, a MAX/FALL setup objective can affect a MIN capture-clock arrival and RISE upstream arrivals.

For no eligible endpoint states, return an absent objective and absent gradients rather than suggesting a meaningful zero-valued derivative exists.

## Query execution and locking

```text
report_gradients(options):
    validate options and acquire the Timer mutex
    call private _update_timing()
    reject the request if effective CPPR mode is enabled
    start a fresh gradient workspace and query generation
    build a complete smooth-arrival task graph
    build objective evaluation / gradient seeding task
    build reverse gradient tasks
    run the gradient Taskflow and wait
    construct an owned GradientReport of cell/net arc gradients
    release transient tasks, pin adjoints, and derivative weights
```

Call private `_update_timing()` while holding the lock. Calling public `update_timing()` from inside the lock would attempt to lock the same mutex again.

Check CPPR after applying pending builders, since a queued `cppr()` operation may change the effective mode. An unsupported request produces an explicit error rather than silently disabling CPPR or treating its correction as constant.

Use a separate Taskflow for the gradient query. The ordinary `_taskflow` is cleared by `_update_timing()` and cannot be treated as a persistent tape. A repeated gradient query must work even when ordinary timing is already current and `_update_timing()` returns immediately.

No gradient flag is added to public `update_timing()`. Share narrow dependency-building helpers where useful, while keeping gradient execution under the new API.

## Smooth forward arrival pass

For destination state `v = (pin, el, trf)`, enumerate the same valid arrival candidates as `Arc::_fprop_at()`:

```text
c_i = smooth_arrival[source_pin, el, frf]
    + stored_delay[arc, el, frf, trf]
    + delta_delay[arc, el, frf, trf]
```

Exclude loop-breaker arcs and undefined source/delay combinations. Include a supplied primary-input seed wherever the ordinary arrival calculation includes one. Do not turn an absent value into zero or introduce candidates for undefined transitions.

Use:

```text
softmax_tau(c) = tau * log(sum_i exp(c_i / tau))
softmin_tau(c) = -tau * log(sum_i exp(-c_i / tau))

MAX arrival = softmax_tau(candidates)
MIN arrival = softmin_tau(candidates)
```

Use stable logsumexp evaluation by subtracting the largest scaled input. An empty candidate set remains absent. A single candidate passes through exactly, plus its perturbation. Reductions are unnormalized: subtracting `tau * log(candidate_count)` would define a different approximation.

Store or recompute the local derivatives using the final reduction value:

```text
MAX: w_i = exp(c_i / tau)  / sum_j exp(c_j / tau)
MIN: w_i = exp(-c_i / tau) / sum_j exp(-c_j / tau)
```

Weights are nonnegative and sum to one. Every valid candidate participates; comparing against the current winner and discarding losers would defeat smooth differentiation. An incremental logaddexp accumulator is acceptable only if its reverse calculation accounts for the complete reduction.

Soft arrivals are stored separately from exact `Pin::At` values. The exact `pi_arc` pointer cannot represent the blended path of a soft arrival. Path reports and CPPR must not follow a fabricated winner for these soft values.

This design differentiates the objective with respect to the computed delay values while treating slew, load, Liberty interpolation, and RC parameter calculations as fixed. Derivatives with respect to slew, load, cell size, resistance, or capacitance would require extending the differentiated computation through those delay models.

## Smooth endpoint slack and objectives

Use the same eligible output and test endpoint states as ordinary timing. Preserve endpoint multiplicity, legal transitions, and availability. Endpoint sorting and top-k path enumeration are unnecessary for the smooth objective.

For an output endpoint with required time `R`:

```text
MIN slack = smooth_A - R
MAX slack = R - smooth_A
```

For a sequential test with fixed library constraint `H` or `U`, capture clock edge `crf`, and period `P`:

```text
MIN required = smooth_A[clock, MAX, crf] + H
MAX required = smooth_A[clock, MIN, crf] + P - U
```

Construct smooth slack from these smooth required times and the smooth data arrival. Reusing the exact test `_rat` would incorrectly remove capture-clock derivatives. Keep the rising/falling trigger selection identical to `Test::_fprop_rat()`.

For all selected endpoint states `e`, define:

```text
smooth_TNS = sum_e -tau_o * log(1 + exp(-slack_e / tau_o))
smooth_WNS = -tau_o * log(sum_e exp(-slack_e / tau_o))
```

Use stable softplus and logsumexp implementations. Smooth TNS includes small negative contributions from satisfied endpoints. Smooth WNS is a soft minimum of signed slack and may be positive for sufficiently satisfied designs; do not add a zero-clipping operation absent from the existing signed-worst-slack calculation.

When filters are omitted, TNS sums the selected combinations. WNS takes one global soft minimum across all selected endpoint states. WNS values or gradients from different combinations must not be simply added.

The returned derivative belongs to the smooth objective. Smoothing introduces a bias depending on temperature and the number and depth of competing paths. Report the temperatures so results are reproducible, and evaluate exact timing separately when assessing circuit changes.

## Reverse-mode derivative rules

This is a specialized reverse-mode AD engine with explicit rules for addition, smooth reduction, endpoint subtraction, and objective reduction. It does not initially require converting every OpenTimer float to a generic AD scalar or tracing unrelated parsing and graph-edit operations.

The objective derivative with respect to each selected endpoint slack is:

```text
TNS: q_e = sigmoid(-slack_e / tau_o)
WNS: q_e = exp(-slack_e / tau_o) / sum_j exp(-slack_j / tau_o)
```

Seed endpoint arrival adjoints as follows:

| Endpoint analysis | Data-arrival seed | Capture-clock-arrival seed for a test |
| --- | --- | --- |
| MIN / hold | `+q_e` | `-q_e` into MAX clock state |
| MAX / setup | `-q_e` | `+q_e` into MIN clock state |

Output required times are fixed, so primary outputs have no clock seed. Sum seeds if a pin participates in several endpoints or appears in both data and clock roles.

For each arrival reduction, reverse propagation is:

```text
contribution = adjoint[destination_state] * candidate_weight
delay_gradient[arc, el, frf, trf] += contribution
adjoint[source_state] += contribution
```

The equality of the two contributions follows from `candidate = source_arrival + arc_delay`: both local derivatives are one. Pin arrival adjoints are required to carry sensitivity upstream, but they are implementation state rather than the primary reported gradient. Parallel uses of the same arc-delay state accumulate into that state's delay gradient.

Only initialize objective seeds after the full smooth forward pass has completed. This handles direct clock dependencies before arrival adjoints are propagated upstream.

## Taskflow graph reuse and parallel correctness

Reuse timing-graph dependency construction rather than retaining handles into the cleared ordinary Taskflow. The existing forward graph is a scheduling graph; its lambdas do not contain automatic derivative implementations.

For every active forward dependency `u -> v`, create the reverse dependency `grad(v) -> grad(u)`. Forward and reverse tasks operate on all four states of their pin; local derivative edges retain the source and destination transition labels.

```mermaid
flowchart LR
    FA["Smooth arrivals: source pins"] --> FB["Smooth arrivals: downstream pins"]
    FB --> O["Endpoint slack, objective, and gradient seeds"]
    O --> GB["Gradient tasks: downstream pins"]
    GB --> GA["Gradient tasks: source pins"]
```

Use a global dependency boundary between completion of forward tasks, objective seeding, and reverse execution. This avoids a gradient task running before an unrelated endpoint has contributed its seed.

For race-free accumulation:

1. Give each arrival candidate its own contribution slot in the query workspace.
2. A destination gradient task writes the candidate contribution that serves both as an arc-delay contribution and an upstream pin-adjoint contribution.
3. A source gradient task waits for downstream gradient tasks and gathers upstream contributions, plus its endpoint seeds, in a deterministic order.
4. Arc gradients are reduced from their candidate contribution slots in a deterministic order; each pin task writes only its own internal adjoint values.

No two downstream tasks write into the same pin accumulator concurrently. Parallel arcs and different transition pairs have separate slots.

Forward tasks store reduction weights in slots indexed by arc/state rather than appending to one unsynchronized global tape. Pin-level dependencies may include extra scheduling edges that have no numeric arrival derivative; only recorded valid candidates carry gradient contributions.

Build a full relevant DAG for the initial implementation, using the same loop-breaker decisions as exact timing. Gradients describe that fixed loop-broken timing model. The latest `_fprop_cands` and `_bprop_cands` are neither persistent nor necessarily complete enough for a fresh gradient query.

## Storage and lifecycle

Proposed transient query storage:

```text
smooth arrival[split][transition]: optional<double>
pin arrival adjoint[split][transition]: optional<double>
arc delay gradient[split][input transition][output transition]: optional<double>
gradient query generation / validity marker
```

The workspace contains candidate references and weights, endpoint seeds, pin adjoints, contributions per candidate, accumulated arc gradients, and Taskflow handles. A flat workspace indexed by pin/arc and timing state avoids enlarging every permanent `Pin` and `Arc`. The owned report copies the final cell/net arc gradients out of this workspace.

Every request clears previous gradients and computes a fresh smooth pass. Changing filters, temperatures, or objective requires a new result even if exact timing has not changed. Pending builders or an ordinary timing update invalidate any cached gradient view. Returned report snapshots remain valid because they own their data.

Expected work is linear in the active pin/arc states and eligible endpoints, with a small constant for two splits and two transitions. Temporary storage is linear in valid arrival candidates. One reverse sweep computes sensitivities for every valid cell and net arc-delay state for one selected scalar objective.

## Planned source changes

| File | Responsibility |
| --- | --- |
| New `ot/timer/gradient.hpp` | Objective/options/report types and small gradient-state definitions. |
| `ot/timer/timer.hpp` | Public gradient API and private workspace/task helpers. |
| `ot/timer/arc.hpp` | Arc identity/access needed to snapshot cell/net delay-gradient results; permanent gradient arrays are not required. |
| New `ot/timer/gradient.cpp` | Smooth forward pass, endpoint equations, objective reduction, reverse rules, task creation, and result snapshots. |
| `ot/timer/timer.cpp` | Narrow dependency-helper reuse and invalidation integration. Existing ordinary update semantics are preserved. |
| `CMakeLists.txt` | Include the new source and register gradient tests. |
| New `unittest/gradient.cpp` and fixtures | Mathematical checks, finite differences, query lifecycle, and parallel consistency. |

Reuse existing pin/arc/net/library access from Timer. Neither `Path` nor `SfxtCache` is needed for the initial gradient objective. No new behavior in the shell is required for the first C++ API version.

## Incremental gradients: future extension

The first version always recomputes the complete gradient query, even when ordinary timing was updated incrementally. This is necessary until gradient invalidation is implemented independently.

Future incremental execution must track:

- Changes to arc delays, boundary arrivals, endpoint constraints, and topology.
- Downstream changes to smooth arrivals and reduction weights.
- Changes to endpoint seeds and upstream gradient contributions.
- Contributions from unaffected regions, including removal of old contributions before replacement.
- Query identity: objective, filters, temperatures, units, and CPPR mode.

Smooth WNS has a global normalization denominator, so changing one endpoint can change every endpoint seed. Reusing ordinary frontiers alone is insufficient. A future incremental implementation must compare its results with a fresh complete gradient query.

## CPPR: future extension

Initially reject effective CPPR-enabled gradient requests explicitly. Do not silently disable it or freeze its correction while claiming a complete derivative.

Current CPPR traces selected paths and uses suffix-tree calculations. Future support must define how path selection and common-clock-path correction interact with soft arrivals, then differentiate the resulting correction consistently. Treat this as a separate model extension with its own gradient checks.

## Verification and acceptance criteria

Use explicit per-arc-delay perturbations in the smooth evaluator as a test seam. Check each reported derivative against a central finite difference of the same smooth objective, with a sweep of perturbation sizes to distinguish truncation and numerical error. Use double precision and documented absolute/relative tolerances.

Required coverage:

- Single path: known cell-arc and net-arc gradient signs and magnitudes.
- Branching and reconvergence: accumulation and soft reduction weights, including equal candidates.
- Inversion: gradients land on the correct input/output RISE/FALL delay entries.
- Setup and hold: opposite-split capture-clock derivatives; shared clock/data ancestry accumulates both effects.
- All nine filter selections: each filter is absent or one of its two values.
- TNS combined objective/gradients equal sums of selected combination results; WNS uses a global soft minimum.
- Negative, positive, and zero exact slack; finite smooth values near boundaries.
- Valid zero-sensitivity arcs, undefined delay states, parallel arcs, and no eligible endpoints.
- Repeated queries without lineage, objective/filter/temperature changes, and requests after an incremental ordinary update.
- CPPR enabled directly or by a pending builder is rejected explicitly.
- Same numerical results with sequential and parallel task execution; no shared accumulator races.
- Exact timing reports are unchanged after a gradient query.
- Consistent results under time-unit conversion when temperatures and perturbation sizes are converted too.

Acceptance means the new API returns the smooth objective and verified cell/net arc-delay sensitivities, reuses timing dependency construction for Taskflow scheduling, and explicitly documents full recomputation and CPPR limitations. Pin-arrival adjoints remain internal reverse-propagation state. Acceptance does not imply incremental gradient support or derivatives with respect to physical design parameters such as gate size, resistance, capacitance, or placement.
