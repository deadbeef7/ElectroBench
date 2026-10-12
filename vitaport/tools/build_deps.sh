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
# Why SceShaccCgExt is required: the ElectroBench shaders use Vita-specific Cg
# intrinsics (bit_cast and friends) that ship inside the compiler module's
# hidden sce_intrinsics set. Older builds of build_deps.sh stubbed
# sceShaccCgExt{Enable,Disable}Extensions with no-ops, which left the loaded
# libshacccg.suprx's stdlib load disabled — every runtime GLSL compile then
# failed with "unexpected undeclared identifier 'bit_cast'" (black frames on
# device and in Vita3K alike). The real SceShaccCgExt library enables the
# extension flags and injects the intrinsic source through taiHEN at runtime.
# It depends on the taiHEN user API, so stub archives for both are generated
# here (vitasdk does not ship them) from the upstream taiHEN exports.yml.
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

# ------------------------------------------------------------------- taiHEN
# SceShaccCgExt links against taiHEN's user API (taiGetModuleInfo, taiInject*).
# vitasdk does not ship taiHEN stub archives, so generate them from the upstream
# exports.yml with the dummy-ELF trick: vita-elf-export needs *some* ELF to read
# the YAML against; the import YAML it writes lists every function NID, which is
# all vita-libs-gen needs to build the stub archives.
if [ ! -f "$VITASDK/arm-vita-eabi/lib/libtaihen_stub.a" ]; then
  cd "$WORKDIR"
  if [ ! -d taihen ]; then
    git clone --depth 1 https://github.com/yifanlu/taiHEN.git taihen
  fi
  mkdir -p taihen-stubs && cd taihen-stubs
  cat > dummy.c <<'EOF'
/* Empty module: drives vita-elf-export for taiHEN stub generation only. */
EOF
  $VITASDK/bin/arm-vita-eabi-gcc -c dummy.c -o dummy.o
  $VITASDK/bin/arm-vita-eabi-gcc -nostdlib dummy.o -o taihen-dummy.elf
  $VITASDK/bin/vita-elf-export kernel taihen-dummy.elf ../taihen/exports.yml taihen-import.yml
  rm -rf out
  $VITASDK/bin/vita-libs-gen taihen-import.yml out
  make -C out VITASDK="$VITASDK" install
fi

# -------------------------------------------------------------- SceShaccCgExt
# The real extension library (bythos14/SceShaccCgExt). Built as a static lib the
# app links; at runtime sceShaccCgExtEnableExtensions() taiHEN-patches the
# already-loaded SceShaccCg module to (a) re-enable compiler extension support
# and (b) inject sce_intrinsics (bit_cast etc.) into the compiler's stdlib.
# NOTE: upstream's TAI_CONTINUE call site predates GCC's zero-arg prototype
# checks; a sed keeps the manual hook-chain walk GCC >= 14 accepts.
cd "$WORKDIR"
if [ ! -d SceShaccCgExt ]; then
  git clone --depth 1 https://github.com/bythos14/SceShaccCgExt.git
fi
cd SceShaccCgExt
if ! grep -q "ElectroBench port" src/shacccg_ext.c 2>/dev/null; then
  # Replace the TAI_CONTINUE(...) call with an equivalent inline chain walk.
  # Done in python because the replacement is multi-line and sed -i gets
  # unreadable at that point.
  python3 - src/shacccg_ext.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\treturn TAI_CONTINUE(void *, hookRef, param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);"
new = (
    "\tstruct _tai_hook_user *cur_ = (struct _tai_hook_user *)hookRef;\n"
    "\tstruct _tai_hook_user *nxt_ = (struct _tai_hook_user *)cur_->next;\n"
    "\ttypedef void *(*shacc_ext_fun8_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);\n"
    "\treturn (void *)(nxt_ == ((void *)0)\n"
    "\t\t? ((shacc_ext_fun8_t)cur_->old)(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8)\n"
    "\t\t: ((shacc_ext_fun8_t)nxt_->func)(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8));"
)
assert old in s, 'TAI_CONTINUE anchor not found in ' + p
open(p, 'w').write(s.replace(old, new, 1))
open(p, 'a').write('\n/* ElectroBench port: TAI_CONTINUE rewritten for modern GCC (see above). */\n')
PYEOF
fi
rm -rf build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" >/dev/null
make -C build -j"$(nproc)"
cp build/libSceShaccCgExt.a "$VITASDK/arm-vita-eabi/lib/"
cp include/shacccg_ext.h "$VITASDK/arm-vita-eabi/include/"

# ---------------------------------------------------------------- vitaShaRK
cd "$WORKDIR"
if [ ! -d vitaShaRK ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaShaRK.git
fi
cd vitaShaRK

cat > source/shacccg_ext.h <<'EOF'
/* Stand-in for SceShaccCgExt's <shacccg_ext.h> — the real header is installed
 * into $VITASDK above, so this copy is only a fallback that matches it. */
#ifndef _SHACCCG_EXT_H_
#define _SHACCCG_EXT_H_

int sceShaccCgExtEnableExtensions(void);
void sceShaccCgExtDisableExtensions(void);

#endif
EOF
rm -f source/shacccg_ext_stub.c
# CPPFLAGS (not CFLAGS) so vitaShaRK's own optimisation flags stay intact.
# -Isource makes the injected shacccg_ext.h visible to the <> include.
make -j"$(nproc)" CPPFLAGS=-Isource install

# ------------------------------------------------------------------- vitaGL
cd "$WORKDIR"
if [ ! -d vitaGL ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaGL.git
fi
cd vitaGL
# NO_SPLASHSCREEN=1 compiles out the animated vitaGL splashscreen the library
# otherwise draws at vglInit() (source/vgl.c: invoke_splashscreen()). The app
# runs bare on its own scene 2/scene 4 rendering instead of showing a spinning
# DNA logo before frame one.
# LOG_ERRORS=1 routes vitaGL's shader-compiler and GXM failure paths through
# sceClibPrintf, so a scene that dies before its first frame (the stuck-splash
# symptom) reports the failing shader instead of hanging silently.
make NO_SPLASHSCREEN=1 LOG_ERRORS=1 -j"$(nproc)" install

echo
echo "vitaGL + vitaShaRK + SceShaccCgExt + taiHEN stubs installed into $VITASDK"
ls -l "$VITASDK/arm-vita-eabi/lib/libvitaGL.a" "$VITASDK/arm-vita-eabi/lib/libvitashark.a"
