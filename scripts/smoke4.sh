#!/bin/bash
# Smoke-render all four scenes headless and report each one's real exit code.
# Guards the BUILD-P24 window change: --width/--screenshot force
# gWindowedMode=true, so every render here takes the branch the new
# SDL_GetWindowSize readback does NOT touch. If a scene still fails, the change
# broke something outside the fullscreen path.
set -u
cd /home/daytona/codebase
OUT=/tmp/fix/smoke
mkdir -p "$OUT"
: > "$OUT/exits"
for pair in "og-only:1" "scene-only:2" "pool-only:3" "pole-only:4"; do
  flag="${pair%%:*}"; n="${pair##*:}"
  DISPLAY=:99 ./build/ElectroBench "--$flag" --width 320 \
    --screenshot "$OUT/s$n" --shot-time 1 > "$OUT/s$n.log" 2>&1
  rc=$?
  echo "scene $n (--$flag) exit=$rc" >> "$OUT/exits"
  if [ -f "$OUT/s$n" ]; then
    head -c 2 "$OUT/s$n" > "$OUT/s$n.hdr"
    echo "scene $n size=$(head -1 "$OUT/s$n.hdr"; sed -n '2p' "$OUT/s$n" | tr -d ' ')" >> "$OUT/exits"
  fi
done
echo SMOKEDONE >> "$OUT/exits"