#!/bin/bash
# Render ONE frame of ONE scene at a given sim time, into its own file.
#   render1.sh OUTDIR WIDTH SCENEFLAG TIME [shotflag]
# SCENEFLAG is --scene-only | --pool-only | --pole-only | --og-only
# One process per shot: the --screenshot path is used verbatim, so a single
# multi-time run overwrites itself (that is why render_sweep.sh forks).
set -u
OUT="$1"; W="$2"; SCENE="$3"; T="$4"; SHOTFLAG="${5:---shot-times}"
BIN=/home/daytona/codebase/build/ElectroBench
mkdir -p "$OUT"
echo "=== $SCENE t=$T w=$W ===" >> "$OUT/log"
DISPLAY=:99 "$BIN" "$SCENE" --width "$W" \
  --screenshot "$OUT/t$T" "$SHOTFLAG" "$T" >> "$OUT/log" 2>&1
mv "$OUT/t$T" "$OUT/t$T.ppm" 2>/dev/null
echo "=== done t=$T (exit $?) ===" >> "$OUT/log"
