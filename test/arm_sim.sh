#!/usr/bin/env bash
# The engine test built for aarch64 (64-bit ARM) with the plugin's own target flags (static musl, so it runs without
# an ARM glibc) and run under qemu: checks the aarch64 code generation. Needs Zig and qemu binfmt (qemu-aarch64
# registered via /proc/sys/fs/binfmt_misc; if `./engine_test` fails with "exec format error", re-register with
# `docker run --privileged --rm tonistiigi/binfmt --install arm64`). There is no armv7 qemu on this machine, so this
# script covers aarch64 only; armv7 correctness rides on the on-device run (TESTING.md). Timings under qemu mean
# nothing: the CPU check is tools/bench.sh on the device.
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
ZIG="${ZIG:-python3 -m ziglang}"
TGT="-target aarch64-linux-musl -ffp-contract=off"
mkdir -p build/armsim
$ZIG cc $TGT -O2 -w -std=gnu11 -Isrc -Ithird_party/mpc-vst-plugins/wrapper -static $SRCS test/engine_test.c -lm \
  -o build/armsim/engine_test
file -b build/armsim/engine_test | grep -q aarch64
(cd build/armsim && ./engine_test)
