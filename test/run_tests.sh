#!/usr/bin/env bash
# MPC Synthdrums offline tests (x86): the engine test, under AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
python3 tools/gen_params.py --check
mkdir -p build/host
gcc -O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ithird_party/mpc-vst-plugins/wrapper \
  $SRCS test/engine_test.c -lm -o build/host/engine_test
build/host/engine_test
