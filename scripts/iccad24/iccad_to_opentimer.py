#!/usr/bin/env python3
"""Convert an ICCAD 2024 gate-sizing netlist into Verilog that OpenTimer can read.

OpenTimer's Verilog reader is a plain tokenizer: it has no buses, no bit-selects
and no escaped identifiers. This script
  * expands bus declarations (input/output/wire [hi:lo] x) into single bits,
  * maps every bit-select x[i] to the plain identifier x__i__,
  * maps escaped identifiers (\\name ) to plain identifiers,
and writes a name map (OpenTimer instance name -> original name) so a sizing
result can be written back in the contest .size format.

It also rewrites the SDC for OpenTimer, which has no set_false_path: ports named
in "set_false_path -from [get_ports {...}]" get no input arrival time, so no
timing propagates from them (the effect of a -from false path).

Usage: iccad_to_opentimer.py <in.v> <out.v> <out.instmap> [<in.sdc> <out.sdc>]
"""
import fnmatch
import re
import sys

src, dst, mapfile = sys.argv[1:4]
text = open(src).read()

# Drop comments.
text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
text = re.sub(r"//[^\n]*", " ", text)

used = set()
escaped_map = {}


def plain(name):
    """Turn any identifier (with optional bit select) into an OpenTimer-safe one."""
    out = re.sub(r"\[(\d+)\]", r"__\1__", name)
    out = re.sub(r"[^A-Za-z0-9_]", "_", out)
    return out


# Escaped identifiers: backslash up to the next whitespace.
def replace_escaped(m):
    original = m.group(1)
    if original not in escaped_map:
        escaped_map[original] = plain(original)
    return " " + escaped_map[original] + " "


text = re.sub(r"\\(\S+)\s", replace_escaped, text)

# Bit-selects on ordinary identifiers: name[12] -> name__12__
text = re.sub(r"\b([A-Za-z_][A-Za-z0-9_$]*)\s*\[(\d+)\]", lambda m: m.group(1) + "__" + m.group(2) + "__", text)

# Expand range declarations: (input|output|wire) [hi:lo] a, b;
def expand_decl(m):
    kind, hi, lo, names = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    step = -1 if hi >= lo else 1
    bits = []
    for n in [x.strip() for x in names.split(",") if x.strip()]:
        bits += [f"{n}__{i}__" for i in range(hi, lo + step, step)]
    return f"{kind} " + ", ".join(bits) + ";"


bus_ports = {}
for m in re.finditer(r"\b(input|output)\s*\[\s*(\d+)\s*:\s*(\d+)\s*\]\s*([^;]+);", text):
    hi, lo = int(m.group(2)), int(m.group(3))
    step = -1 if hi >= lo else 1
    for n in [x.strip() for x in m.group(4).split(",") if x.strip()]:
        bus_ports[n] = [f"{n}__{i}__" for i in range(hi, lo + step, step)]

text = re.sub(r"\b(input|output|inout|wire)\s*\[\s*(\d+)\s*:\s*(\d+)\s*\]\s*([^;]+);", expand_decl, text)

# Expand bus names in the module header port list.
head = re.search(r"\bmodule\s+(\w+)\s*\((.*?)\)\s*;", text, flags=re.S)
ports = []
for p in [x.strip() for x in head.group(2).split(",") if x.strip()]:
    ports += bus_ports.get(p, [p])
text = text[:head.start()] + f"module {head.group(1)} (" + ",\n".join(ports) + ");" + text[head.end():]

# Collect instance names (cell inst ( ... );) for the map back to original names.
reverse_escaped = {v: k for k, v in escaped_map.items()}
inst_map = []
for m in re.finditer(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", text, flags=re.M):
    cell, inst = m.group(1), m.group(2)
    if cell in ("module", "input", "output", "wire", "inout"):
        continue
    inst_map.append((inst, reverse_escaped.get(inst, inst)))

names = [a for a, _ in inst_map]
if len(names) != len(set(names)):
    sys.exit("name collision after flattening; aborting")

open(dst, "w").write(text)
with open(mapfile, "w") as f:
    for a, b in inst_map:
        f.write(f"{a} {b}\n")
print(f"ports {len(ports)}, instances {len(inst_map)}, escaped names {len(escaped_map)}", end="")

if len(sys.argv) > 5:
    sdc_in, sdc_out = sys.argv[4:6]
    inputs = []
    for m in re.finditer(r"\binput\s+([^;]+);", text):
        inputs += [x.strip() for x in m.group(1).split(",") if x.strip()]
    sdc = re.sub(r"#[^\n]*", "", open(sdc_in).read())
    false_from = set()
    for m in re.finditer(r"set_false_path\s+-from\s+\[get_ports\s+\{([^}]*)\}\]", sdc):
        false_from.update(m.group(1).split())
    supported = ("create_clock", "set_input_delay", "set_input_transition", "set_output_delay", "set_load")
    out = []
    for line in sdc.splitlines():
        line = line.strip()
        if not line.startswith(supported):
            continue
        if line.startswith("set_input_delay") and "[all_inputs]" in line and false_from:
            kept = [p for p in inputs if not any(fnmatch.fnmatchcase(p, pat) for pat in false_from)]
            line = line.replace("[all_inputs]", "[get_ports {" + " ".join(kept) + "}]")
        out.append(line)
    open(sdc_out, "w").write("\n".join(out) + "\n")
    dropped = [p for p in inputs if any(fnmatch.fnmatchcase(p, pat) for pat in false_from)]
    print(f", false-path ports without arrival: {len(dropped)}", end="")
print()
