#!/bin/sh
# ============================================================
# Verify the Vita shader tree, from the branch root:
#
#   sh vitaport/tools/check_shaders.sh
#
# Two checks, both must pass:
#   1. vitaport/shaders is exactly what tools/make_vita_shaders.py generates
#      today (so a shader edit cannot silently drift away from the port),
#   2. every generated shader is valid GLSL ES 1.00 according to
#      glslangValidator -- the language level vitaGL's GLSL translator accepts,
#      and the closest thing to the SceShaccCg front end that runs on a host.
# ============================================================
set -eu

cd "$(dirname "$0")/../.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

VITA_SHADER_OUT="$TMP" python3 vitaport/tools/make_vita_shaders.py > /dev/null

echo "--- 1. regenerated tree matches the committed one"
# The diff output is captured in a variable, not written under $TMP: writing it
# there would make it part of the tree being compared.
if delta=$(diff -ru vitaport/shaders "$TMP"); then
  echo "OK   vitaport/shaders is up to date ($(find vitaport/shaders -name '*.glsl' | wc -l) shaders)"
else
  echo "FAIL vitaport/shaders differs from generated output:"
  echo "$delta"
  exit 1
fi

echo "--- 2. glslangValidator (GLSL ES 1.00)"
fails=0
for f in vitaport/shaders/*/*.glsl; do
  case "$f" in
    *_vert.glsl) stage=vert ;;
    *_frag.glsl) stage=frag ;;
    *) echo "FAIL unrecognised shader name: $f"; fails=$((fails + 1)); continue ;;
  esac
  if glslangValidator -S "$stage" "$f" > "$TMP/gsv.txt" 2>&1; then
    echo "OK   $f"
  else
    echo "FAIL $f"
    cat "$TMP/gsv.txt"
    fails=$((fails + 1))
  fi
done

echo "--- 3. no GLSL 3.30-only syntax left for vitaGL's translator"
# vitaGL rewrites `attribute`/`varying` into Cg for SceShaccCg and deletes
# #version/#extension/precision, but it does NOT translate `in`/`out`,
# `layout(location=)` or the 3.30 overloaded texture()/fwidth(). Any of those
# reaching SceShaccCg is a shader that cannot compile on the device, so treat
# them as errors here rather than finding out on hardware.
for f in vitaport/shaders/*/*.glsl; do
  hits=$(grep -nE '^[[:space:]]*(layout|flat|in|out)[[:space:](]|(^|[^A-Za-z_])texture[[:space:]]*\(|(^|[^A-Za-z_])fwidth[[:space:]]*\(' "$f" || true)
  if [ -n "$hits" ]; then
    echo "FAIL $f still uses 3.30-only syntax:"
    echo "$hits"
    fails=$((fails + 1))
  else
    echo "OK   $f (1.00-style declarations only)"
  fi
done

if [ "$fails" -ne 0 ]; then
  echo "SHADER_CHECK_FAILURES=$fails"
  exit 1
fi
echo "SHADER_CHECK_EXIT=0"
