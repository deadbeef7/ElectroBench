#!/bin/bash
# Render a sweep of loop frames for scene 4, one process per sim time, so each
# frame lands in its own file (the --screenshot path is used verbatim, so a
# single multi-time run overwrites itself).
# usage: render_sweep.sh OUTDIR WIDTH T1,T2,T3...
set -u
OUT="$1"; W="$2"; TIMES="$3"
BIN=/home/daytona/codebase/build/ElectroBench
mkdir -p "$OUT"
IFS=',' read -ra TS <<< "$TIMES"
for t in "${TS[@]}"; do
  echo "=== rendering t=$t ===" >> "$OUT/log"
  DISPLAY=:99 "$BIN" --pole-only --width "$W" \
    --screenshot "$OUT/t$t" --shot-times "$t" >> "$OUT/log" 2>&1
  mv "$OUT/t$t" "$OUT/t$t.ppm" 2>/dev/null
done
echo "=== sweep done ===" >> "$OUT/log"