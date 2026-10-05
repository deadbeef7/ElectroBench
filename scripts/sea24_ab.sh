#!/bin/bash
# BUILD-P24 A/B, done properly this time.
#
# Scene 2 is NOT pixel-reproducible here: --shot-time captures the first frame
# whose scene clock crosses the requested second, and on a software rasteriser a
# frame takes ~2 s, so each run lands on a different animation phase. So both
# variants are rendered at three phases and compared on SUMMARY statistics.
set -u
cd /home/daytona/codebase
OUT=/tmp/fix/p24ab
rm -rf "$OUT"; mkdir -p "$OUT"
BIN=./build/ElectroBench
CUR=shaders/ps14/sea_frag.glsl
W=760

cp "$CUR" /tmp/fix/sea_frag.P24.glsl
git show HEAD:shaders/ps14/sea_frag.glsl > /tmp/fix/sea_frag.BEFORE.glsl

shot () {   # $1 = variant, $2 = time
  rm -f "$OUT/$1-$2"
  DISPLAY=:99 "$BIN" --scene-only --width "$W" \
    --screenshot "$OUT/$1-$2" --shot-time "$2" >> "$OUT/$1.log" 2>&1
  mv "$OUT/$1-$2" "$OUT/$1-$2.ppm" 2>/dev/null
  echo "done $1 t=$2" >> "$OUT/progress.log"
}

cp /tmp/fix/sea_frag.BEFORE.glsl "$CUR"
for T in 32 36 40; do shot before "$T"; done

cp /tmp/fix/sea_frag.P24.glsl "$CUR"
for T in 32 36 40; do shot fix "$T"; done

# sanity: the tree must be left on the fix, with no diagnostic tag left in it
grep -c DIAGNOSTIC "$CUR" > "$OUT/tagcount.txt" 2>&1
echo ALLDONE >> "$OUT/progress.log"