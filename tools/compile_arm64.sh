#!/usr/bin/env bash
# Compile and link MPC Synthdrums for aarch64 (64-bit ARM), inside the MM-303 build image.
# Invoked by build.sh with ARCH=aarch64 via `docker run mm303:aarch64-audio` (native GCC 11.5, glibc 2.31).
#   tools/compile_arm64.sh <outdir> <mpc-vst-dir> <src...>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$1"; MPV="$2"; shift 2
CC="${CC:-aarch64-linux-gnu-gcc}"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -ffp-contract=off -DNDEBUG -w -std=gnu11"

mkdir -p "$OUT"
OBJS=""
for f in "$@"; do
  o="$OUT/$(echo "$f" | tr / _).o"
  $CC $COMMON -Ibuild -Isrc -I"$MPV/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > "$OUT/exports.map"
$CC -shared -fPIC -Wl,--no-undefined -Wl,--version-script="$OUT/exports.map" -Wl,--gc-sections -Wl,-s \
  $OBJS -lm -ldl -lpthread -o "$OUT/synthdrums.so"
echo "-> $OUT/synthdrums.so ($(file -b "$OUT/synthdrums.so" | cut -d, -f1-2))"
