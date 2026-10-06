#!/bin/bash
# Render scene 2 -- the PS1.4 dusk ocean (--scene-only) -- at several sim times,
# one process per shot, sequentially inside one job. With the BUILD-P28
# deterministic shot clock each capture is an exact function of the requested
# sim time, so two runs of the same code at the same t MUST produce
# byte-identical PPMs.
# usage: sea_sweep.sh OUTDIR WIDTH T1,T2,T3
set -u
OUT="$1"; W="$2"; TIMES="$3"
BIN=/home/daytona/codebase/build/ElectroBench
mkdir -p "$OUT"
IFS=',' read -ra TS <<< "$TIMES"
for t in "${TS[@]}"; do
  echo "=== rendering scene-only t=$t w=$W ===" >> "$OUT/log"
  DISPLAY=:99 "$BIN" --scene-only --width "$W" \
    --screenshot "$OUT/t$t" --shot-times "$t" >> "$OUT/log" 2>&1
  mv "$OUT/t$t" "$OUT/t$t.ppm" 2>/dev/null
done
echo "=== sweep done ===" >> "$OUT/log"
