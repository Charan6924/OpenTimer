#!/usr/bin/env python3
"""Write "<cell> <family>" lines from a CircuitOps libcell_properties.csv.

The ICCAD 2024 contest allows swapping an instance to any library cell with the
same func_id; this turns that rule into a family file for ot-sizer / ot-gsizer.

Usage: make_families.py <libcell_properties.csv[.bz2]> <out.txt>
"""
import bz2
import csv
import sys

src, dst = sys.argv[1:3]
opener = bz2.open if src.endswith(".bz2") else open
with opener(src, "rt") as f, open(dst, "w") as out:
    for row in csv.DictReader(f):
        if row["libcell_name"].startswith("sram"):
            continue                         # macros are not sized
        out.write(f"{row['libcell_name']} f{row['func_id']}\n")
