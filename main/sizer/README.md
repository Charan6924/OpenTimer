# ot-sizer

`ot-sizer` resizes gates to reduce setup violations using OpenTimer's incremental timing. It tries other drive strengths of each gate on a violating late path, keeps the changes that improve late TNS, and then downsizes gates with spare slack to recover area.

## Build

```
mkdir -p build && cd build
cmake ..
make ot-sizer
```

The binary lands in `bin/ot-sizer`.

## Run

```
bin/ot-sizer <lib> <verilog> <spef | -> <sdc> [max_iters] [top_k_paths]
```

| Argument | Meaning | Default |
|---|---|---|
| `lib` | Liberty file, used for both early and late timing | |
| `verilog` | gate-level netlist | |
| `spef` | parasitics, or `-` to skip | |
| `sdc` | timing constraints | |
| `max_iters` | maximum sizing rounds | 50 |
| `top_k_paths` | number of late paths searched for critical gates | 200 |

The program prints late TNS, late WNS, early TNS, area, and leakage before and after sizing, and writes the sized netlist to `sized.v` in the current directory.

## Example

```
cd example/map9v3
../../bin/ot-sizer osu018_stdcells map9v3.v map9v3.spef map9v3.sdc
```

On this design, late TNS improves from −917 to −418 and late WNS from −22.2 to −12.0, while area grows from 10026 to 10858.

## Limitations

The sizer is greedy and stops at a local minimum. It optimizes setup only, so check the early TNS it reports for hold regressions. OpenTimer does not check max-transition or max-capacitance limits, and the sizer does not either. Each cell must exist in both the early and late library.

