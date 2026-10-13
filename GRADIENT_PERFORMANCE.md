# Gradient optimization benchmark results

Implemented on branch `perf/gradient-taskflow`, in the managed worktree
`/Users/charan/.codex/worktrees/c87b/OpenTimer`. Baseline source: `6e09382`;
optimization implementation: `1a9c582`. Both libraries use C++17 and `-O2`.
All benchmark processes ran sequentially without a timeout on the 16 GB M2 Air.

## Matched superblue4 comparisons

The original executor ignores `set_num_threads`; its actual default is eight
workers on this host. These before/after cases use eight actual workers on both
versions, MAX timing, both transitions, and temperatures 0.001 in library units.
The original has one fresh run per objective; the candidate has two.

| Objective | Original gradient time | Optimized gradient time | Observed speedup | Original peak footprint | Optimized peak footprint |
|---|---:|---:|---:|---:|---:|
| TNS | 385.6 s | 21.6–39.4 s | 9.8–17.8× | 16.77 GiB | 9.7 GiB |
| WNS | 503.9 s | 47.7–79.1 s | 6.4–10.6× | 16.80 GiB | 11.7 GiB |

Wall times vary with laptop load, memory pressure, and cache state. These are
observed runs, not a stable performance guarantee. The unchanged profiler run
previously took 312 s for TNS, whereas this fresh baseline took 386 s. Report
construction remains included in `gradient_seconds`. Peak footprint is the
macOS kernel's whole-process physical-footprint high-water mark; RSS is also
saved but is inadequate by itself for judging compressed-memory workloads.

## Five completed TNS designs

The superblue4 baseline below is fresh. Other original times come from the
previous saved runs, whose requested four-thread setting actually used eight
executor workers. Those rows are historical comparisons, not fresh paired trials.

| Benchmark | Original gradient time | Optimized gradient time | Optimized total time | Optimized peak footprint |
|---|---:|---:|---:|---:|
| superblue4 | 385.6 s | 21.6–39.4 s | 42.0–69.2 s | 9.7 GiB |
| superblue16 | 704.5 s | 64.6 s | 97.6 s | 11.7 GiB |
| superblue5 | 638.1 s | 92.0 s | 145.0 s | 11.8 GiB |
| superblue1 | 891.3 s | 144.6 s | 274.2 s | 13.3 GiB |
| superblue3 | 1916.7 s | 188.8 s | 288.4 s | 13.5 GiB |

All five have zero smooth TNS and zero derivatives, as in the original reports.
Their objectives, hard TNS/WNS, circuit sizes, report sizes, optional-state
counts, and nonzero counts agree with the originals. None contains SPEF data.

## Actual-worker WNS comparisons

| Workers | Gradient time | Total time | Peak footprint | Nonzero gradients |
|---|---:|---:|---:|---:|
| 1 | 62.8 s | 87.9 s | 11.2 GiB | 876 |
| 2 | 46.6 s | 65.6 s | 11.2 GiB | 876 |
| 4 | 34.7 s | 52.8 s | 11.3 GiB | 876 |
| 8 | 47.7–79.1 s | 66.0–103.2 s | 11.7 GiB | 876 |

Every optimized WNS run matches all **876 nonzero gradients**, their parameter
identifiers, the objective, and full report counts. Maximum absolute difference
across runs: **4.44e-16**. Comparison tolerance: absolute 1e-12 plus relative
1e-9. The default remains four workers; these few measurements do not establish
a universal best worker count for other circuits or machines.

## Implementation and correctness

- Indexed task-owned smooth pin/arc state removes shared-container mutation and its locks.
- Compact arc validity masks preserve missing versus zero states while reducing storage.
- Small local derivatives are recomputed from saved smooth inputs during active reversal instead of storing every LUT query twice.
- Worker-local LUT adjoints are combined after reverse tasks finish.
- Zero smooth-TNS seeds skip derivative propagation and defer unused reverse arrays, while exact required times and complete reports are still maintained.
- Executor replacement honors positive requested worker counts, preserves queued builders, and exposes actual counts.
- Benchmark output adds peak footprint and sparse nonzero records for numerical comparisons.

**79/79 CTest checks passed**, including the former 64 checks, physical
finite differences, seven example designs, and real-worker-count checks.
The automated baseline-reference test checks optional arc states, names,
LUT uses, zero/active TNS, and WNS; it rejects a deliberately corrupted fixture.
An independent read-only reviewer found no critical math or ownership defects.
Its important reference-comparison test gap was fixed and the whole suite rerun.

Rulings: preserve standalone diagnostic derivative maps for explicit model
calls; keep exact required-time propagation and opposite-split clock state.
These preserve supported diagnostics and ordinary/setup/hold timing behavior.

Deferred: the two older manual forward checks remain obsolete and are outside
the maintained CTest set. Inactive RC finalization still traverses zero adjoints
to construct complete RC reports. Large SPEF benchmarking is outside this batch;
small RC finite-difference tests pass.

## Results and reproduction

Raw JSON, nonzero parameter records, logs, and appended measurement CSV:
`/Users/charan/OpenTimer/results/gradient-optimization/`.
The labeled `summary.csv` and `summary.json` include all 13 successful runs and
their numerical comparison results. Original benchmark files remain available.

In this worktree:

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=17
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 1
c++ -std=c++17 -O2 -pthread -I . scripts/gradient_benchmark.cpp lib/libOpenTimer.a -o build/gradient-benchmark
./build/gradient-benchmark
```

Edit `BenchmarkSettings` in the C++ runner to choose the circuit, objective,
workers, temperatures, and output paths. No Python benchmark runner is required.
