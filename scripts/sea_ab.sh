#!/bin/bash
# A/B the sea-shader change.
#
# Scene 2 cannot be compared pixel-for-pixel here: --shot-time captures the
# first frame whose scene clock crosses the requested second, so on a software
# rasteriser each run lands on a slightly different animation phase (frames
# take ~2 s at 760 px). On a real GPU that quantisation is ~16 ms. So this
# renders several phases per variant and compares SUMMARY statistics, which are
# phase-insensitive, rather than diffing one frame against another.
set -u
cd /home/daytona/codebase
OUT=/tmp/fix/ab24
mkdir -p "$OUT"
BIN=./build/ElectroBench
CUR=shaders/ps14/sea_frag.glsl

render_set () {   # $1 = tag
  for T in 32 36 40; do
    DISPLAY=:99 "$BIN" --scene-only --width 760 \
      --screenshot "$OUT/$1-$T" --shot-time "$T" >> "$OUT/$1.log" 2>&1
    mv "$OUT/$1-$T" "$OUT/$1-$T.ppm" 2>/dev/null
    echo "done $1 t=$T" >> "$OUT/progress.log"
  done
}

# --- BEFORE: the committed shader ---
git show HEAD:shaders/ps14/sea_frag.glsl > /tmp/fix/sea_frag.BEFORE.glsl
cp /tmp/fix/sea_frag.BEFORE.glsl "$CUR"
render_set before

# --- AFTER: the working-tree shader ---
cp /tmp/fix/sea_frag.P24.glsl "$CUR"
render_set after

# --- restore the working tree exactly as it was ---
cp /tmp/fix/sea_frag.RESTORE.glsl "$CUR"

echo ALLDONE >> "$OUT/progress.log"
