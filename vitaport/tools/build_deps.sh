#!/bin/sh
# ============================================================
# ElectroBench Vita port - build the two vendor libraries the game links against.
#
#   vitaShaRK  runtime GLSL -> SceGxmProgram shader compiler (drives SceShaccCg)
#   vitaGL     the OpenGL implementation the port renders through (OpenGL -> SceGxm)
#
# Neither library is packaged by vdpm (the public [vita] repository only carries
# glm/libftpvita/vitagprof), so both are built here from upstream sources into
# $VITASDK. Run this once before `make -f vitaport/Makefile`.
#
# Usage:  VITASDK=/usr/local/vitasdk sh vitaport/tools/build_deps.sh
#
# Why vitaShaRK needs a patch: upstream includes <shacccg_ext.h> (from the
# out-of-tree SceShaccCgExt project) and calls sceShaccCgExt{Enable,Disable}
# Extensions(). Those two entry points only exist in newer copies of the
# on-device libshacccg.suprx and are absent from vitaSDK's libSceShaccCg_stub.a,
# so linking an app against the stock build fails. vitaGL only needs
# shark_compile_shader_extended()/shark_init()/shark_end(), all of which are
# implemented on top of the standard sceShaccCg API that vitaSDK *does* ship.
# The script therefore drops in a minimal shacccg_ext.h plus no-op definitions of
# the two extension entry points. This is the pre-extension code path that older
# vitaGL releases used, so shader compilation still goes through SceShaccCg.
#
# Environment:
#   VITASDK   toolchain prefix (required)
#   WORKDIR   scratch directory for the clones (default /tmp/electrobench-vita-deps)
# ============================================================
set -eu

: "${VITASDK:?set VITASDK to your vitasdk install, e.g. /usr/local/vitasdk}"
WORKDIR="${WORKDIR:-/tmp/electrobench-vita-deps}"

PATH="$VITASDK/bin:$PATH"
export VITASDK PATH

mkdir -p "$WORKDIR"
cd "$WORKDIR"

# ---------------------------------------------------------------- vitaShaRK
if [ ! -d vitaShaRK ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaShaRK.git
fi
cd vitaShaRK

cat > source/shacccg_ext.h <<'EOF'
/* Minimal stand-in for SceShaccCgExt's <shacccg_ext.h>.
 *
 * Only the two extension entry points vitaShaRK references are declared here.
 * They are provided as no-ops by source/shacccg_ext_stub.c: the standard
 * sceShaccCg API shipped by vitaSDK is sufficient to compile GLSL, and the
 * extension functions are unavailable in libSceShaccCg_stub.a. */
#ifndef _SHACCCG_EXT_H_
#define _SHACCCG_EXT_H_

void sceShaccCgExtEnableExtensions(void);
void sceShaccCgExtDisableExtensions(void);

#endif
EOF

cat > source/shacccg_ext_stub.c <<'EOF'
/* No-op SceShaccCgExt extension toggles (see source/shacccg_ext.h).
 *
 * Enabling the extended compiler is an opt-in fast path in newer
 * libshacccg.suprx builds; without it SceShaccCg still compiles GLSL through
 * the standard sceShaccCgCompileProgram path, which is what vitaGL uses. */
void sceShaccCgExtEnableExtensions(void) {}
void sceShaccCgExtDisableExtensions(void) {}
EOF

# CPPFLAGS (not CFLAGS) so vitaShaRK's own optimisation flags stay intact.
# -Isource makes the injected shacccg_ext.h visible to the <> include.
make -j"$(nproc)" CPPFLAGS=-Isource install

# ------------------------------------------------------------------- vitaGL
cd "$WORKDIR"
if [ ! -d vitaGL ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaGL.git
fi
cd vitaGL
make -j"$(nproc)" install

echo
echo "vitaGL + vitaShaRK installed into $VITASDK"
ls -l "$VITASDK/arm-vita-eabi/lib/libvitaGL.a" "$VITASDK/arm-vita-eabi/lib/libvitashark.a"
