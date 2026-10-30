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

Without `--families`, cells are grouped by `cell_footprint`, or by the drive-strength suffix in the name (`NAND2_X2`, `NAND2X2`, `NAND2x1p5_ASAP7_75t_R`). Cells that are not in any family are never resized. Only cells with the same pins and the same timing arcs (related pin and timing type) share a family: `repower_gate` maps pins by name, and the arcs keep apart cells with the same pins and different behavior, such as a flip-flop and a latch.

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

`scripts/iccad24/prepare.sh` converts the [contest benchmarks](https://github.com/ASU-VDA-Lab/2024_ICCAD_Contest_Gate_Sizing_Benchmark) for OpenTimer: it flattens buses and escaped names in the Verilog, removes input arrival times from `set_false_path -from` ports, and writes the contest's swap rule as a family file. Combinational cells can swap to a cell with the same `func_id`. Sequential cells and macros keep their cells, as the contest requires, so the script leaves them out of the file. It finds them in the Liberty files, because `func_id` alone puts flip-flops and latches with the same pins in one group.

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

## Results

Both sizers ran with default options. The ICCAD 2024 designs use all ASAP7 libraries, `--no-preset-clear` and `--time-limit 900`. vga_lcd (TAU 2015, in `benchmark/`) uses its late library, its SPEF and `--time-limit 300`. TNS and WNS are OpenTimer's late (setup) values in ps.

### Timing

| Design | Gates | Sizable | Start TNS | Start WNS | ot-sizer TNS | ot-sizer WNS | ot-gsizer TNS | ot-gsizer WNS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| NV_NVDLA_partition_m | 27,553 | 24,490 | -72,736 | -365.4 | -66,193 | -286.1 | **-16,143** | **-195.8** |
| hidden1 | 38,089 | 26,346 | -158,442 | -370.2 | -4.1 | -2.4 | **0** | **+60.6** |
| NV_NVDLA_partition_p | 79,919 | 59,011 | -3.19e6 | -812.5 | -9.19e5 | -226.5 | **-14,124** | **-84.3** |
| vga_lcd | 139,529 | 139,510 | -4.96e7 | -5,106.8 | -4.91e7 | -4,897.2 | **-3.43e7** | **-3,674.7** |

### Area and runtime

| Design | Start area | ot-sizer area | ot-sizer time | ot-sizer memory | ot-gsizer area | ot-gsizer time | ot-gsizer memory |
|---|---:|---:|---:|---:|---:|---:|---:|
| NV_NVDLA_partition_m | 2362.47 | 2365.04 | 858 s, 15,212 trials | 0.36 GB | 2497.41 | 128 s, 48 gradients, 286 updates | 0.77 GB |
| hidden1 | 5610.62 | 5612.91 | 126 s, 1,923 trials | 0.72 GB | 5613.42 | 55 s, 5 gradients, 303 updates | 1.58 GB |
| NV_NVDLA_partition_p | 10487.9 | 10490.5 | 903 s (limit), 4,024 trials | 1.45 GB | 10580.8 | 810 s, 33 gradients, 1,703 updates | 3.11 GB |
| vga_lcd | no area in library | | 108 s, 492 trials | 1.77 GB | | 309 s (limit), 21 gradients, 44 updates | 5.10 GB |

Times are wall clock and include reading the design. A trial is one swap followed by a timing update. An update in `ot-gsizer` is one timing update for a batch of swaps.

### Observations

- `ot-gsizer` reaches a lower TNS on every design: 4.1x lower than `ot-sizer` on NV_NVDLA_partition_m, 65x lower on NV_NVDLA_partition_p, and zero on hidden1, where it also takes less than half the time. On vga_lcd it improves TNS by 31% from the start, against 1% for `ot-sizer`.
- The greedy sizer is limited by trials. A trial costs about 0.06 s on NV_NVDLA_partition_m and hidden1 and 0.2 s on NV_NVDLA_partition_p and vga_lcd, while one gradient report (1.4 to 9.3 s) ranks every swap in the design at once.
- `ot-gsizer` stops early on NV_NVDLA_partition_m at -16.1k, because the swaps with the largest predicted gain no longer improve the exact TNS. Larger steps (`--max-step`) or buffering may help beyond this point.
- `ot-gsizer` trades some area for timing: +5.7% on NV_NVDLA_partition_m, +0.9% on NV_NVDLA_partition_p and +0.05% on hidden1.
- On vga_lcd the greedy sizer finds only 3 improving swaps and stops after two iterations.
- Neither sizer optimizes hold (early) TNS. `ot-gsizer` leaves it unchanged on every design, and `ot-sizer` makes it slightly worse on NV_NVDLA_partition_m (-164.9 to -166.8).
- `ot-gsizer` uses 2 to 3 times the memory of `ot-sizer`, for the gradient report.
- Every ICCAD `.size` file contains all instance names of the contest's reference `.size`, keeps every sequential cell and macro, and swaps combinational cells only within their `func_id`.
- ariane136 is not included. OpenTimer finds about 20,000 timing loops in it, so its TNS is not reliable.

These are OpenTimer numbers with ideal wires on the ICCAD designs. The contest's OpenSTA flow estimates wire RC and also scores slew, capacitance and leakage, so its scores will differ. Score the `.size` files with the contest's evaluation scripts before comparing with contest results.

To reproduce one ICCAD design:

```
scripts/iccad24/prepare.sh <benchmark repo> iccad hidden1
L=<benchmark repo>/platform/ASAP7/lib
for SZ in ot-sizer ot-gsizer; do
  bin/$SZ $(for f in $L/*.lib; do echo --lib $f; done) \
    --verilog iccad/hidden1/hidden1.v --sdc iccad/hidden1/hidden1.sdc \
    --families iccad/asap7_families.txt --instmap iccad/hidden1/hidden1.instmap \
    --no-preset-clear --time-limit 900 --out hidden1_$SZ
done
```

## Limitations

- Both sizers optimize setup only. Hold slack (early TNS) is reported but not protected.
- OpenTimer does not check max-transition or max-capacitance limits, and neither sizer does.
- The benchmarks come without parasitics, so OpenTimer times ideal wires, while the contest's OpenSTA evaluation estimates wire RC from the placement. If OpenROAD can export those estimated parasitics as SPEF, pass the file with `--spef`.
- OpenTimer reads leakage as zero for the ASAP7 libraries, so area is the cost being recovered, not leakage.
