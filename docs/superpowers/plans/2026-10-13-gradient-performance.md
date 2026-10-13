# Gradient performance implementation plan

> Execute inline with the executing-plans workflow; review the whole change after correctness checks.

**Goal:** Reduce full physical-gradient time and memory while preserving objective values, gradients, complete reports, and exact timing state.

**Architecture:** Recompute small local LUT derivatives during active reverse propagation instead of caching every query. Store forward smooth values in indexed, task-owned arrays; use compact validity masks and worker-local LUT accumulation. Preserve diagnostic local-derivative calls. Keep existing Taskflow dependencies and cross-split clock dependencies.

**Spec:** ../specs/2026-10-13-gradient-performance.md (recommendations approved in the conversation).

**Constraints:** C++17, existing Taskflow, no new dependencies; isolated worktree; benchmarks sequential with no timeout. Preserve full report names, optional arc states, LUT usage metadata, and no-LUT collection mode.

## Review focus

- Zero TNS with positive smooth slack versus a hard-zero/smooth-negative endpoint.
- Setup/hold clock seeds on the opposite split, and constraint-only LUT uses.
- Worker counts changed with pending builder tasks; valid executor after allocation failure.
- Concurrent map/container mutation and reduction-order differences in LUT adjoints.
- RC-only delay paths, undefined arrival states, scalar tables, knots, extrapolation, and mutation between calls.

## Tasks

1. Baseline and regression coverage: build existing physical-gradient checks; add real worker-count tests and reference report comparisons (including inactive TNS and active WNS/TNS). Record baseline before implementation.
2. Compact indexed workspace: replace shared smooth maps with pin/arc arrays; replace optional arc scalars with values plus validity masks; store smooth constraints per test; preserve standalone derivative diagnostics.
3. LUT hot path: add direct local-derivative outputs; retain standalone cache behavior; use reverse recomputation, per-worker used-table/adjoint storage, and final reduction. Skip derivative propagation with zero seeds while preserving exact timing and full report shape.
4. Thread configuration: safely own a replaceable executor; honor requested positive worker count, reject zero, and expose actual count. Update benchmark measurement to capture footprint and verify outputs.
5. Verification and benchmarks: finite differences, all seven example checks, complete serial CTest, before/after large TNS and WNS cases, repeated timings, actual-worker scaling. Save comparison results and document limits; review the branch before completion.

## Progress

- Baseline commit: 6e09382. Current checkout's untracked C++ benchmark runner copied into worktree; data is linked read-only by usage.
- User approved implementing the prior recommendations in a new worktree and running benchmarks. Implementation and plan execution proceed without another approval gate.
- Tasks 1–4 complete: original 64-test baseline passes; worker-count check failed on original code and passes on the candidate. Compact indexed state, local derivative outputs/recomputation, worker-local LUT reduction, zero-seed skipping, and executor configuration implemented.
- Correctness gate: 79/79 CTest passes after the review fix. Existing finite differences and seven example checks are registered with CTest.
- Independent review: no critical math or ownership findings. Fixed the important test gap by comparing full reports against a fixture generated with the original library; a deliberately corrupted fixture fails and the genuine fixture passes.
- Ruling: preserve low-level diagnostic derivative maps for explicit standalone model calls; full timer gradient passes bypass them. This maintains existing diagnostic checks without putting their locks back into the production hot path.
- Ruling: keep exact required-time propagation even for inactive TNS, and retain both splits for clock dependencies. Skipping those would change ordinary timing reports or setup/hold gradients.
- Deferred: two pre-existing obsolete manual forward checks still use the old internal workspace and executor access; maintained CTest gradient checks supersede them. Inactive RC model finalization still traverses zero adjoints to produce complete RC reports; it is outside this measured no-SPEF bottleneck.
- Benchmarks in progress: sequential, no timeout, actual 8-worker before/after comparisons; active WNS sparse numerical records and 1/2/4/8 worker measurements. First optimized superblue4 TNS gradient call: 39.42 seconds; all counts and objectives match the original run.

- Task 5 complete: all 13 sequential runs succeeded; all five TNS reports match originals and all WNS runs match 876 nonzero values. Maximum absolute difference 4.440892098500626e-16. Final results: GRADIENT_PERFORMANCE.md and labeled summary CSV/JSON in the main results directory.
