#!/bin/bash
# Prepare ICCAD 2024 gate-sizing benchmarks for OpenTimer.
# Usage: scripts/iccad24/prepare.sh <benchmark repo> <output dir> [design ...]
#   <benchmark repo>: a clone of ASU-VDA-Lab/2024_ICCAD_Contest_Gate_Sizing_Benchmark
set -e
REPO=$1; OUT=$2; shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
DESIGNS=${@:-$(ls "$REPO/design" | grep -v -e README -e unzip)}
mkdir -p "$OUT"
python3 "$HERE/make_families.py" "$REPO/IR_Tables/NV_NVDLA_partition_m/libcell_properties.csv.bz2" "$OUT/asap7_families.txt"
for D in $DESIGNS; do
  mkdir -p "$OUT/$D"
  for f in "$REPO/design/$D"/*.bz2; do bzip2 -dc "$f" > "$OUT/$D/$(basename "${f%.bz2}").orig"; done
  python3 "$HERE/iccad_to_opentimer.py" "$OUT/$D/$D.v.orig" "$OUT/$D/$D.v" "$OUT/$D/$D.instmap" \
                                        "$OUT/$D/$D.sdc.orig" "$OUT/$D/$D.sdc"
done
