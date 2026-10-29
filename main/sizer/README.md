# Gate sizers: ot-sizer and ot-gsizer

Two gate sizers built on OpenTimer. Both reduce late (setup) TNS by swapping cells within a family of interchangeable cells and then recover area, and both accept a change only if OpenTimer's exact timing confirms it.

| Binary | Source | How it chooses moves |
|---|---|---|
| `ot-sizer` | `greedy_sizer.cpp` | Tries every alternative cell of the most critical gates, one timing update per trial. |
| `ot-gsizer` | `gradient_sizer.cpp` | Predicts the effect of every swap from one gradient report (`report_gradients`) and applies the best ones as a batch. |

## Build

The build needs CMake, a C++17 compiler, and `tclsh`. Run it from the repository root.

```
mkdir -p build && cd build
cmake ..
make ot-sizer ot-gsizer
```

The binaries land in `bin/`.

## Run

```
bin/ot-sizer  --lib <file> [--lib <file> ...] --verilog <file> --sdc <file> [options]
bin/ot-gsizer --lib <file> [--lib <file> ...] --verilog <file> --sdc <file> [options]
```

The original positional form still works: `bin/ot-sizer <lib> <verilog> <spef|-> <sdc> [max_iters] [top_k_paths]`.

| Option | Meaning | Default |
|---|---|---|
| `--lib <file>` | Liberty file, read for both early and late timing; repeat for several libraries | |
| `--verilog`, `--sdc`, `--spef` | design files (`--spef` optional) | |
| `--families <file>` | lines `<cell> <family>` defining interchangeable cells | footprint or name pattern |
| `--instmap <file>` | lines `<instance> <original name>` for the `.size` output | |
| `--no-preset-clear` | skip asynchronous preset/clear arcs, as OpenSTA does by default | off |
| `--time-limit <s>` | stop optimizing after this many seconds | none |
| `--threads <n>` | OpenTimer worker threads | OpenTimer default |
| `--max-iters <n>` | setup-fixing iterations | 50 |
| `--no-area-recovery` | skip area recovery | off |
| `--out <prefix>` | writes `<prefix>.v` (sized netlist) and `<prefix>.size` | `sized` |
| `--top-k <n>` | `ot-sizer`: violating paths searched per iteration | 200 |
| `--gates-per-iter <n>` | `ot-sizer`: critical gates tried per iteration | 100 |
| `--tau <t>` | `ot-gsizer`: smoothing temperature (library time units) | 0.1 × median output slew |
| `--max-step <n>` | `ot-gsizer`: largest drive-strength step per move | 2 |

Without `--families`, cells are grouped by `cell_footprint`, or by the drive-strength suffix in the name (`NAND2_X2`, `NAND2X2`, `NAND2x1p5_ASAP7_75t_R`). Only cells with identical pin names share a family, because `repower_gate` maps pins by name.

## How the sizers work

`ot-sizer` ranks gates on violating late paths by the negative slack that runs through them and tries every other cell of the family for the top `--gates-per-iter` gates, keeping the best. Area recovery downsizes gates with at least 5% of a clock period of slack in adaptive batches, one timing update per batch. A single trial costs an incremental update of the gate's fanin and fanout cones, which approaches a full update on large designs, so the number of trials is what bounds the runtime.

`ot-gsizer` runs one backward pass of a smooth late-TNS objective per iteration. For each gate and each candidate cell it predicts the objective change to first order:

```
dJ = sum over input pins      dJ/dC_pin   * (C_pin(new) - C_pin(old))
   + sum over arcs and states dJ/dDelay   * (Delay_new(s, C) - Delay_old(s, C))
                            + dJ/dSlewOut * (Slew_new(s, C)  - Slew_old(s, C))
```

where `s` and `C` are the arc's current input slew and output load. It applies the swaps with the largest predicted gain together, checks the exact late TNS with one timing update, and halves the batch until TNS improves. Area recovery uses the same prediction to pick downsizes that should not hurt timing.

## ICCAD 2024 gate-sizing benchmarks

`scripts/iccad24/prepare.sh` converts the [contest benchmarks](https://github.com/ASU-VDA-Lab/2024_ICCAD_Contest_Gate_Sizing_Benchmark) for OpenTimer: it flattens buses and escaped names in the Verilog, removes input arrival times from `set_false_path -from` ports, and writes the contest's swap rule (same `func_id`) as a family file.

```
scripts/iccad24/prepare.sh <benchmark repo> iccad NV_NVDLA_partition_m
L=<benchmark repo>/platform/ASAP7/lib
bin/ot-gsizer $(for f in $L/*.lib; do echo --lib $f; done) \
  --verilog iccad/NV_NVDLA_partition_m/NV_NVDLA_partition_m.v \
  --sdc     iccad/NV_NVDLA_partition_m/NV_NVDLA_partition_m.sdc \
  --families iccad/asap7_families.txt --instmap iccad/NV_NVDLA_partition_m/NV_NVDLA_partition_m.instmap \
  --no-preset-clear --time-limit 900 --out NV_NVDLA_partition_m
```

`NV_NVDLA_partition_m.size` is in the contest's output format and can be scored with the contest's OpenROAD evaluation scripts.

## Limitations

- Both sizers optimize setup only. Hold slack (early TNS) is reported but not protected.
- OpenTimer does not check max-transition or max-capacitance limits, and neither sizer does.
- The benchmarks come without parasitics, so OpenTimer times ideal wires, while the contest's OpenSTA evaluation estimates wire RC from the placement. If OpenROAD can export those estimated parasitics as SPEF, pass the file with `--spef`.
- OpenTimer reads leakage as zero for the ASAP7 libraries, so area is the cost being recovered, not leakage.
