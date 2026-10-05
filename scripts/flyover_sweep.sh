#!/bin/bash
# Render the TEN scene-1 flyover frames, one process per shot.
#
# Scene 1 only accepts a single --shot-time, so a multi-frame loop is ten
# separate runs. --width and --height are independent for scene 1 (unlike the
# three GL 3.3 scenes, which derive their height from the width), so both are
# passed explicitly to land on 760x428 and match the other loops.
#
# Times are spread across all three acts of UpdateFlyoverCamera() in main.cxx:
#   act 1  reveal   t < 7
#   act 2  runway   7 <= t < 34
#   act 3  pullback t >= 34 (ends at 47)
set -u
OUT=/tmp/fix/fly
BIN=/home/daytona/codebase/build/ElectroBench
mkdir -p "$OUT"
for T in 2 5 7.5 12 17 22 27 34.5 40 45; do
  [ -s "$OUT/f$T.ppm" ] && { echo "skip t=$T (exists)"; continue; }
  echo "=== t=$T start ===" >> "$OUT/sweep.log"
  DISPLAY=:99 "$BIN" --og-only --width 760 --height 428 \
    --screenshot "$OUT/f$T" --shot-time "$T" >> "$OUT/sweep.log" 2>&1
  mv "$OUT/f$T" "$OUT/f$T.ppm" 2>/dev/null
  echo "=== t=$T exit=$? ===" >> "$OUT/sweep.log"
done
echo ALLDONE >> "$OUT/sweep.log"
