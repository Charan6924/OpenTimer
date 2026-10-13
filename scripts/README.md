# Gradient benchmark

Edit `BenchmarkSettings` in `gradient_benchmark.cpp` to select the benchmark,
objective (`TNS`/`WNS`), split (`MIN`/`MAX`/`both`), threads, temperatures, and
input/output paths. Defaults select `superblue4`, late TNS, and four threads.

Build and run from the repository root:

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=17
cmake --build build --target OpenTimer -j 4
c++ -std=c++17 -O2 -pthread -I . scripts/gradient_benchmark.cpp lib/libOpenTimer.a -o build/gradient-benchmark
./build/gradient-benchmark
```

The program loads early/late Liberty, Verilog, SDC, and optional `rc.spef`.
LEF/DEF are placement files and are not consumed by the timing API. Inputs
are unchanged. Runs have no time limit.

Each completed run appends a row to `results/optimized-gradient-benchmarks.csv` for
comparison. It also writes `results/<benchmark>-<objective>.json`, replacing
that JSON file on the next matching run. Change the settings and rebuild to
run another benchmark or objective; run one process at a time.

Results include objective values, circuit/report sizes, runtime, and peak RSS
in MiB. Loading time includes the initial timing update because file reading
is deferred. `gradient_seconds` measures `report_gradients()` only; total time
also includes finite-value checks and cleanup, excluding output writing.
Peak RSS covers the entire process, not just gradient storage. On macOS,
`peak_footprint_mib` also records the kernel's peak physical footprint;
unsupported platforms report null. `actual_threads` records the executor's
worker count. Thread counts must be positive.
`pre_gradient_peak_rss_mib` is the high-water mark before the gradient call.
Caught computation errors are saved; forced termination cannot save a row.

With `save_nonzero_gradients` enabled, a `.json.nonzero.txt` file records each
nonzero gradient and its parameter identifier for numerical comparisons.
This writing happens after the measured gradient call. Compare objective
values, report counts, and these records when checking two implementations.

Temperatures use library time units, recorded as `time_unit_seconds`. Only
`superblue18` includes a SPEF; zero slew can trigger RC derivative validation.
CPPR is unsupported. Finite-value checks do not prove derivative correctness.
