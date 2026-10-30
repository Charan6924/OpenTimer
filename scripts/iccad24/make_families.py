#!/usr/bin/env python3
"""Write "<cell> <family>" lines from a CircuitOps libcell_properties.csv.

The ICCAD 2024 contest allows swapping a combinational instance to any library
cell with the same func_id, and does not allow resizing sequential cells or
macros. This turns that rule into a family file for ot-sizer / ot-gsizer.

func_id alone is not enough: it puts flip-flops and latches with the same pins
in one group (DFFHQx4 with DHLx1-3), so sequential cells are found in the
Liberty files (cells with an ff, latch or statetable group) and left out.
Cells that are not in the family file are never resized.

Usage: make_families.py <libcell_properties.csv[.bz2]> <out.txt> <liberty file> [...]
"""
import bz2
import csv
import re
import sys

if len(sys.argv) < 4:
    sys.exit(__doc__)
src, dst, libs = sys.argv[1], sys.argv[2], sys.argv[3:]

cell_re = re.compile(r'^\s*cell\s*\(\s*"?([^"\s)]+)"?\s*\)')
seq_re = re.compile(r'^\s*(ff|latch|ff_bank|latch_bank|statetable)\s*\(')
macro_re = re.compile(r'^\s*is_macro_cell\s*:\s*true')

fixed, cells = set(), set()   # cells that must keep their master; all cells seen
for lib in libs:
    cell = None
    with open(lib) as f:
        for line in f:
            m = cell_re.match(line)
            if m:
                cell = m.group(1)
                cells.add(cell)
            elif cell and (seq_re.match(line) or macro_re.match(line)):
                fixed.add(cell)

opener = bz2.open if src.endswith(".bz2") else open
written = skipped = 0
with opener(src, "rt") as f, open(dst, "w") as out:
    for row in csv.DictReader(f):
        name = row["libcell_name"]
        if name.startswith("sram") or name in fixed:
            skipped += 1                     # macros and sequential cells are not sized
            continue
        if name not in cells:
            print(f"warning: {name} is not in the given Liberty files", file=sys.stderr)
        out.write(f"{name} f{row['func_id']}\n")
        written += 1
print(f"{dst}: {written} sizable cells, {skipped} sequential or macro cells left out")
