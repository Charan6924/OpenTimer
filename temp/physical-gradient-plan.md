# Physical timing gradients

Continue the approved design in GRADIENT_DESIGN.md, with the later decision to retain hard TNS and choose zero derivative at exactly zero slack.

1. Add finite-difference tests for primary input slew, output load, RC resistance/capacitance, and shared delay/slew/constraint LUT entries. Test TNS/WNS and MIN/MAX/rise/fall filters.
2. Add owned result records and per-query slew, arc model-input, constraint, and LUT adjoints. Keep RC local derivatives cached in Rct.
3. Seed constraint derivatives in the objective task; extend existing reverse pin tasks with cell/net slew and delay derivatives. Each destination owns incoming arc contributions. Gather fanout contributions before reversing each pin.
4. Add a final Taskflow task after all reverse pin tasks. Sum net load/impulse/delay contributions, reverse impulse → beta → load-delay → delay → load, then construct owned physical reports and shared LUT sums.
5. Verify finite differences, multiple fanouts and shared tables, serial/parallel results, repeated queries/cache invalidation, exact report preservation, and all example circuits. CPPR and incremental objective-adjoint caching remain unsupported.

Files: ot/timer/gradient.hpp and gradient_context.hpp for storage; new ot/timer/gradient.cpp for reverse model logic; timer.hpp/timer.cpp for hooks; CMakeLists.txt for the new source; temp/physical-gradient-check.cpp for focused finite differences.

## Verification completed

- OpenTimer library builds successfully.
- 136 finite-difference comparisons passed: primary-input slew/output load, branched RC resistance and wire capacitance, shared pin capacitance, shared delay/slew LUT entries, setup/hold constraint entries, clock/data arrival and slew, both objectives and every split/transition filter.
- Seven example circuits passed 108 queries each across serial/parallel execution, all endpoint filters, and temperatures 0.1, 0.001, and 1e-6. Physical report values stayed finite and exact arrival/RAT reports were preserved.
- Full physical reversal is mandatory for every gradient query. Optional LUT-entry collection passed without changing other physical gradients; delay-only nets without asserted slew and invalid-derivative rejection/recovery passed.
- Gradient-only full pin queries reuse unchanged RC caches; model changes still invalidate their RC values and local derivatives.
