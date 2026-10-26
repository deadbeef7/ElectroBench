#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
PSP_OBJ=${PSP_OBJ:-$ROOT/psp/src/psp_scene2.o}
VITA_OBJS=${VITA_OBJS:-$ROOT/build-vita/scene1.o $ROOT/build-vita/scene2.o}
PSP_OD=${PSP_OD:-${PSPDEV:-/usr/local/pspdev}/bin/psp-objdump}
VITA_OD=${VITA_OD:-${VITASDK:-/usr/local/vitasdk}/bin/arm-vita-eabi-objdump}

fail=0
note() { printf '%s\n' "$*"; }

for f in "$PSP_OBJ" $VITA_OBJS; do
  if [ ! -f "$f" ]; then
    note "MISSING $f -- build that port first"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1

# VFPU quad ops the wave kernel must emit: two vsin.q + two vcos.q + three
# vdot.q + six vmul.q + three vadd.q + one vone.q. The threshold is below the
# current 17 so ordinary scheduling changes do not trip it, but far above zero.
psp_quad=$("$PSP_OD" -d "$PSP_OBJ" | grep -cE '\bv(sin|cos|dot|mul|add|one)\.q' || true)
if [ "$psp_quad" -lt 10 ]; then
  note "FAIL PSP: only $psp_quad VFPU quad instruction(s) in psp_scene2.o"
  note "     the wave kernel in psp/src/psp_waves.hxx is not being compiled in"
  fail=1
else
  note "OK   PSP VFPU: $psp_quad quad instructions in psp_scene2.o"
fi

# NEON in the Vita scene translation units. The exact count moves with the
# compiler, so require a healthy fraction of the hundreds both TUs emit (384 in
# scene1.o, 1213 in scene2.o with the pinned SDK).
for obj in $VITA_OBJS; do
  vita_neon=$("$VITA_OD" -d "$obj" | grep -cE '\bv(add|sub|mul|mla)\.f32|\bvld[0-9]|\bvst[0-9]' || true)
  if [ "$vita_neon" -lt 200 ]; then
    note "FAIL Vita: only $vita_neon NEON instruction(s) in $(basename "$obj")"
    note "     -mfpu=neon / -ftree-vectorize is not reaching the scene build"
    fail=1
  else
    note "OK   Vita NEON: $vita_neon NEON instructions in $(basename "$obj")"
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "CODEGEN_CHECK_FAIL"
  exit 1
fi
echo "CODEGEN_CHECK_OK"
