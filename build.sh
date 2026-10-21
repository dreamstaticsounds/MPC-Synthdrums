#!/usr/bin/env bash
# Build MPC Synthdrums for MPC OS (armv7-a hard-float, glibc <= 2.31 so it also loads on MPC OS 2.x).
#   ./build.sh                     (MPC_VST=../mpc-vst-plugins to use another framework checkout)
# Needs: python3 + Pillow and numpy, a host gcc (for shadow_art), and Zig (pip install ziglang) for armv7.
# Output: build/package/Dream Static Sounds - VST - MPC Synthdrums/ (skin + synthdrums.so)
# (the aarch64 branch, with tools/compile_arm64.sh, arrives with the aarch64 task)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
ARCH="${ARCH:-armhf}"                # armhf (Zig, host)
NAME="Dream Static Sounds - VST - MPC Synthdrums"
case "$ARCH" in
  armhf)   OUT="build/arm"     PKGDIR="build/package" ;;
  *) echo "build.sh: ARCH must be armhf (got '$ARCH')" >&2; exit 1 ;;
esac
. ./build_srcs.sh
python3 tools/gen_params.py --check
python3 tools/skin.py --check

mkdir -p build/tools "$OUT"
[ -x build/tools/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/tools/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
rm -rf build/skin
SHADOW_ART="$PWD/build/tools/shadow_art" \
  python3 "$MPC_VST/tools/gen_vst.py" vst.json 2>&1 | grep -v -e "TOUCH" -e "EDGE" || true
[ -f build/params.h ] && [ -d "build/skin/$NAME/Plugin Skins" ]
python3 tools/skin.py "build/skin/$NAME/Plugin Skins"          # the five pages
python3 "$MPC_VST/tools/skin_check.py" "build/skin/$NAME"        # touch boxes, edges, Q-Links, as MPC loads it

TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11"
OBJS=""
for f in $SRCS "$MPC_VST/wrapper/vst2_wrap.c"; do
  o="$OUT/$(echo "$f" | tr / _).o"
  $ZIG cc $TGT $COMMON -Ibuild -Isrc -I"$MPC_VST/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > "$OUT/exports.map"
$ZIG cc $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script="$OUT/exports.map" -Wl,--gc-sections -Wl,-s \
  $OBJS -lm -ldl -lpthread -o "$OUT/synthdrums.so"
PKG="$PKGDIR/$NAME"
rm -rf "$PKGDIR" && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp "$OUT/synthdrums.so" "$PKG/"
echo "-> $PKG"
