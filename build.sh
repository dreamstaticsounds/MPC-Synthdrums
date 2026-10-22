#!/usr/bin/env bash
# Build MPC Synthdrums for MPC OS (armv7-a hard-float, glibc <= 2.31 so it also loads on MPC OS 2.x).
#   ./build.sh                     (MPC_VST=../mpc-vst-plugins to use another framework checkout)
#   ARCH=aarch64 ./build.sh        64-bit ARM (Gen2 devices: MPC Live/One/X/Key 3), compiled inside the
#                                  MM-303 image (Docker, native GCC, glibc 2.31)
# Needs: python3 + Pillow and numpy, a host gcc (for shadow_art), and Zig (pip install ziglang) for armv7;
# for ARCH=aarch64: Docker with the mm303:aarch64-audio image instead of Zig (no Docker needed for armv7).
# Output: build/package/Dream Static Sounds - VST - MPC Synthdrums/ (armv7) or build/package-aarch64/... (aarch64)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
ARCH="${ARCH:-armhf}"                # armhf (Zig, host) | aarch64 (MM-303 image, Docker)
MM303_AARCH64="${MM303_AARCH64:-mm303:aarch64-audio}"
NAME="Dream Static Sounds - VST - MPC Synthdrums"
case "$ARCH" in
  armhf)   OUT="build/arm"     PKGDIR="build/package" ;;
  aarch64) OUT="build/arm64"   PKGDIR="build/package-aarch64" ;;
  *) echo "build.sh: ARCH must be armhf or aarch64 (got '$ARCH')" >&2; exit 1 ;;
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
python3 tools/skin.py "build/skin/$NAME/Plugin Skins"          # the six pages
# touch boxes, edges, Q-Links, as MPC loads it -- popup option rows are named "popup <key> <word>": an open
# list covers the page and takes the touch by design (docs/NOTES.md), so those TOUCH lines are not findings
findings=$(python3 "$MPC_VST/tools/skin_check.py" "build/skin/$NAME" | grep -v "popup lfo" || true)
if [ -n "$findings" ]; then echo "$findings" >&2; echo "build.sh: skin_check found problems" >&2; exit 1; fi

TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11"
if [ "$ARCH" = aarch64 ]; then
  # Native aarch64 GCC against glibc 2.31 inside the MM-303 image (QEMU/binfmt runs the arm64 container here).
  MOUNTS=(-v "$PWD:$PWD")
  case "$MPC_VST" in "$PWD"/*) ;; *) MOUNTS+=(-v "$MPC_VST:$MPC_VST:ro") ;; esac
  docker run --rm "${MOUNTS[@]}" -w "$PWD" --user "$(id -u):$(id -g)" -e HOME=/tmp \
    "$MM303_AARCH64" bash tools/compile_arm64.sh "$OUT" "$MPC_VST" $SRCS "$MPC_VST/wrapper/vst2_wrap.c"
else
  OBJS=""
  for f in $SRCS "$MPC_VST/wrapper/vst2_wrap.c"; do
    o="$OUT/$(echo "$f" | tr / _).o"
    $ZIG cc $TGT $COMMON -Ibuild -Isrc -I"$MPC_VST/wrapper" -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > "$OUT/exports.map"
  $ZIG cc $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script="$OUT/exports.map" -Wl,--gc-sections -Wl,-s \
    $OBJS -lm -ldl -lpthread -o "$OUT/synthdrums.so"
fi
PKG="$PKGDIR/$NAME"
rm -rf "$PKGDIR" && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp "$OUT/synthdrums.so" "$PKG/"
echo "-> $PKG"
