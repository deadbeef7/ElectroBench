#!/bin/sh
# ============================================================
# Verify the Vita shader tree, from the repo root:
#
#   sh buildtools/check_shaders.sh
#
# Two checks, both must pass:
#   1. every committed shader is valid GLSL ES 1.00 according to
#      glslangValidator -- the language level vitaGL's GLSL translator accepts,
#      and the closest thing to the SceShaccCg front end that runs on a host.
# ============================================================
set -eu

cd "$(dirname "$0")/.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The shader tree belongs to the Vita port after the restructure: vita/shaders
# is what the VPK packs and what this check validates.
echo "--- 1. glslangValidator (GLSL ES 1.00)"
fails=0
for f in vita/shaders/*/*.glsl; do
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
for f in vita/shaders/*/*.glsl; do
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
