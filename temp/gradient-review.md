# Gradient implementation review — October 9, 2026

Scope: working-tree physical gradient implementation, including LUTs, cell
delay/slew/constraints, smooth propagation, RC recording/cache/reversal,
objective seeding, task dependencies, and owned report construction. Three
independent read-only reviewers checked the RC, Liberty/forward, and
orchestration portions. No product code was changed during this review.

## Findings

1. **RC report identifiers can be empty (confirmed).**
   `ot/timer/net.cpp:103` default constructs nodes inserted by resistance
   segments without initializing their `_name`. The gradient report uses
   `_name` at `ot/timer/gradient.cpp:229` and `:234`. A zero-capacitance junction
   present only in resistance entries therefore produces `node=""` and empty
   resistance endpoints. Multiple such junctions cannot be distinguished.
   Set names when edge insertion creates nodes, or report the map keys.
   Runtime reproduction using a three-node RC tree emitted:

   ```text
   CAP node=[i]
   CAP node=[]
   CAP node=[o]
   RES from=[i] to=[]
   RES from=[] to=[o]
   ```

2. **Two older forward tests have obsolete expectations (confirmed failures).**
   `temp/smooth-forward-check.cpp:68` and
   `temp/all-example-gradient-check.cpp:185` reconstruct smooth arrival
   candidates from exact `arc->_delay`. The current smooth forward calculation
   uses `context.smooth_delays`, evaluated with smooth input slew. Both tests
   exit with code 3 at their arrival comparison on `simple`. Keep exact delays
   for the hard check and use recorded smooth delays for the smooth check;
   audit subsequent finite-difference expectations as well.

3. **Parallel integration tests share an output file (confirmed).**
   `inttest/tau15.py:20` and `inttest/shell.py:18` both use `.output` inside the
   same benchmark directory. Concurrent AES shell/TAU execution failed with
   timing-length mismatches; both passed when rerun serially. Use distinct
   output paths or a per-benchmark CTest resource lock. This is an existing
   test-harness issue rather than evidence of a gradient defect.

4. **Physical gradient checks are not registered with CTest.**
   The derivative checks live under `temp/` and require explicit compilation
   and execution. The standard 64-test CTest suite alone does not validate
   physical gradients. Register maintained checks for ongoing regression
   coverage.

## Verification

- `cmake --build build -j 4`: successful full build.
- Freshly compiled checks linked against the rebuilt library:
  `lut-derivative-check`, `timing-delay-derivative-check`,
  `timing-slew-derivative-check`, `timing-constraint-derivative-check`, and
  `gradient-validation-check`: all passed.
- `physical-gradient-check`: 136 finite-difference comparisons passed, plus
  the delay-only net check.
- `physical-example-check`: all seven examples passed 108 queries each
  (756 total), covering serial/parallel agreement, endpoint filters,
  temperatures, finite reports, exact arrival/RAT preservation, and optional
  LUT collection.
- The two older forward tests failed as described above.
- Parallel CTest initially exposed the shared-output collision described
  above; both AES tests passed in a serial rerun.
- `ctest --test-dir build --output-on-failure -j 1 --timeout 60`:
  all 64 tests passed in a complete serial run (43.80 seconds).

Scratch binaries, reproductions, and focused test logs are in
`/private/tmp/opentimer-review`. General regression logs are in
`build/Testing/Temporary`.

No additional concrete derivative-equation, reverse traversal, objective-sign,
cache-invalidation, or gradient-task race defect was found. This is review and
test evidence, not a proof of correctness for every possible input. CPPR and
incremental objective-adjoint updates remain outside the supported model.
