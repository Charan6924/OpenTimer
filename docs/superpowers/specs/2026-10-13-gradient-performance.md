# Gradient performance investigation — 2026-10-13

## Measured result

Profiled superblue4 with TNS, MAX, both transitions, temperatures 0.001, and
the existing `-O2` library. A temporary copy of `timer.cpp` adds stage markers;
it is linked ahead of the unchanged static library. No gradient algorithm was
changed. Outputs are separate from the original benchmark comparison CSV.

| Stage | Wall time (seconds) |
|---|---:|
| Allocate dense gradient workspace | 1.23 |
| Insert frontiers | 0.95 |
| Build Taskflow graph | 4.07 |
| Forward tasks | 160.71 |
| Objective seeding | 0.64 |
| Backward tasks | 123.87 |
| Model finalization, including pin/LUT reports | 8.14 |
| Task cleanup | 3.63 |
| Arc report construction | 2.26 |
| Context destruction and return (residual) | 6.77 |
| **Full gradient call** | **312.27** |

Total program time: 333.27 s. TNS and all gradients are zero; hard worst slack
is +2929.84326171875 ps. The benchmark completed successfully.

The forward and backward task phases account for about 91% of the gradient
call. These timings include all work inside their tasks, including maintaining
exact timing state. They are not isolated derivative-arithmetic timings.

One five-second `sample` capture during the forward phase collected 27,416
worker-thread stack samples. 20,715 (75.56%) were in `__psynch_mutexwait`;
2,155 (7.86%) were in `__psynch_mutexdrop`. Of the mutex waits, 18,520 were
under LUT evaluation or the Timing delay/slew/constraint wrappers, which use
the same global `lut_derivatives_mutex`. This is strong evidence of lock
contention in that window, not a whole-run CPU-time percentage or a predicted
speedup. A requested backward sample started after process exit and yielded
no data; there is no sampled attribution for backward execution.

The capture reports physical footprint **13.4G** while the benchmark records
peak RSS 6321.06 MiB. RSS alone is inadequate for judging this workload's
memory burden. The five preallocated arc arrays consume 2.92 GiB on
superblue4, before derivative maps, pin state, Taskflow nodes, circuit data,
or output records. The same arrays require 4.67 GiB on superblue3.

The diagnostic probe confirms **8 actual executor workers** after
`set_num_threads(4)`. That setter currently only logs the requested count;
its worker-setting code is commented out. Existing CSV `threads` fields are
requested counts, not verified executor counts.

## Recommended changes, in priority order

1. **Remove shared derivative-map writes from the forward hot path.**
   `ot/liberty/lut.cpp:230` locks the global LUT map on every lookup.
   `ot/liberty/timing.cpp:446`, `:539`, and `:635` lock it again, find the
   result, and copy it into another ordered map keyed by floating-point
   inputs. Return interpolation value and local derivatives together to the
   owning task. Store only compact records for valid cell-arc states, or
   evaluate local derivatives during reverse propagation from the saved
   smooth inputs. Compare storage versus recomputation costs before choosing;
   naively allocating a large derivative struct for every arc/state could
   worsen memory use. Preserve scalar, extrapolation, and knot conventions.

2. **Use task ownership for pin/arc state and local LUT accumulation.**
   `GradientContext::smooth_slew` locks even for backward reads after the
   forward phase has completed. Replace mutable shared maps for smooth pin
   slews and arc delays with preallocated indexed storage, written by the
   destination pin's task. Task dependencies already order downstream reads.
   Remove mutexes only after eliminating concurrent container mutation.
   For shared LUT-entry adjoints, use worker-local accumulators with a final
   reduction instead of the global mutex in `ot/timer/gradient.cpp:18`.
   Define and test acceptable floating-point reduction-order differences.

3. **Avoid reverse derivative work for zero seeds.**
   After computing the actual smooth TNS endpoint seeds, skip derivative
   propagation when all are zero. At finer granularity, skip cell/LUT/RC
   derivative lookups and accumulation when both delay and slew seeds are
   zero. Currently zero seeds still cause LUT locks and table updates. Keep
   exact required-time maintenance and the documented report structure.
   A zero *hard* TNS alone is insufficient: smooth slack may differ. This
   could remove much of the measured 123.87 s backward cost, but that number
   is not a measured saving and includes ordinary required-time propagation.

4. **Reduce the working set.**
   On this ABI `optional<double>` is 16 bytes; `GradientArcData` contains eight
   of them and is 128 bytes. Five arrays are allocated for every arc. Use
   dense doubles and explicit validity masks where absence is required;
   adjoint contributions can often use plain zero-initialized doubles.
   Allocate RC impulse contributions only for RC-bearing nets. Avoid
   duplicated interpolation structs, and separate optional names/LUT-use
   metadata from the numerical output needed by an optimizer. Preserve the
   full report as an explicit supported mode. Profile memory footprint as
   well as RSS and retain enough state for setup/hold clock dependencies:
   a MAX objective can still depend on MIN clock timing.

5. **Make thread settings real, then measure scaling.**
   Fix executor configuration and report the actual worker count. Compare
   1, 2, 4, and 8 workers after reducing contention; more workers are not
   automatically faster with the current shared locks. Do not change the
   executor while it is executing tasks.

Lower priorities: fuse LUT value/derivative interval calculations, validate
immutable LUT axes once instead of on every lookup, avoid heap allocations
for small arrival candidate lists, and compute all slew weights in one
reduction instead of repeatedly rescaling prior weights. Parallel RC
finalization may matter for SPEF designs, but the profiled design has no RC
trees and finalization is not its primary bottleneck.

## Validation for implementation

Use active WNS and negative-TNS cases, positive-TNS zero-seed cases, existing
finite-difference checks, RC and constraint examples, and exact timing-state
preservation. Compare actual worker counts and numerical results across
thread counts. Rerun superblue4 and at least one active large-circuit
objective, recording stage timings and memory. Repeated baseline/candidate
runs are necessary before claiming a speedup: this diagnostic run and the
previous 338.85 s gradient run already differ without an algorithm change.

## Artifacts

- `results/gradient-profile/superblue4.log`: stage timestamps and resource counters.
- `results/gradient-profile/superblue4-sample.txt`: raw forward thread sample.
- `results/gradient-profile/analysis.json`: derived timings and sample counts.
- `results/gradient-profile/superblue4-TNS.json`: completed diagnostic result.
- `temp/gradient-profile/`: temporary instrumentation, probe, and analysis tools.

Only diagnostic files were added; production source and the original
benchmark results were not changed by this investigation.
