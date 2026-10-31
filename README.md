# MPC Synthdrums

A 12-voice synth drum box for **MPC OS** (Akai MPC / Force), inspired by DSK SynthDrums.
Independent recreation — not affiliated with or endorsed by DSK Music or Akai Professional.
MPC and Force are trademarks of Akai Professional (inMusic Brands).

Version **1.1.0** — 7 skin pages (FILTER, FX, LFO, MASTER, CTRL, KIT, ABOUT), per-voice FX,
tempo-synced delay/stutter, gated verb, master EQ + 12-voice mixer.

You can download the zipped binaries on my Patreon here: https://www.patreon.com/dreamstatic/posts/free-to-all-mpc-173736425

## Requirements

- Python 3 with `Pillow` and `numpy`
- A host C compiler (`gcc`) — used to build the `shadow_art` helper
- For **armhf** (Gen1 devices): Zig (`pip install ziglang`) — cross-compiles on the host
- For **aarch64** (Gen2 devices): Docker with the `mm303:aarch64-audio` image
- The vendored framework at `third_party/mpc-vst-plugins` (included)

## Build (armhf — Gen1: MPC Force, Live/One/X/Key 1–2)

```sh
./build.sh
```

Output: `build/package/Dream Static Sounds - VST - MPC Synthdrums/` (contains `synthdrums.so` + skin).

## Build (aarch64 — Gen2: MPC Live/One/X/Key 3, MPC 500)

```sh
ARCH=aarch64 ./build.sh
```

Requires Docker + binfmt for arm64 (one-time, does not survive reboot):

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

Output: `build/package-aarch64/Dream Static Sounds - VST - MPC Synthdrums/`.

To use a different framework checkout:

```sh
MPC_VST=/path/to/mpc-vst-plugins ./build.sh
```

## Verify the build

```sh
# exactly one exported symbol
nm -D build/arm/synthdrums.so | awk '$2=="T"'   # -> VSTPluginMain
# GLIBC ceiling (must be <= 2.31)
readelf -V build/arm/synthdrums.so | grep GLIBC
file build/arm/synthdrums.so
```

## Tests

All commands run from the repo root.

```sh
# Unit suite (engine, ASan/UBSan) — 282 checks
test/run_tests.sh

# Host test (framework VST2 host) — 77/77
test/host_test.sh

# ARM engine under qemu (aarch64 build)
test/arm_sim.sh

# Parameter / layout / skin checks (also run inside build.sh)
python3 tools/gen_params.py --check
python3 tools/skin.py --check
```

Golden hashes are verified inside the unit suite against `test/goldens/fx-sound.txt`.

## Package and install on a device

Cut a release zip with the framework's packager:

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.1.0 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license MIT \
  -o dist
```

Then install on the device (armv7 or aarch64 zip — the installer reads the arch from the `.so`):

```sh
scp dist/MPC-Synthdrums-1.1.0-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.1.0-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.1.0/install.sh"
```

Note: the skin uses MPC OS 3.x actions, so the device must run MPC OS 3.x even though the binary itself only needs GLIBC 2.27.

## Project layout

- `src/` — engine (`synth.c`, `voice.c`, `filter.c`, `fx.c`, `patches.c`) + `src/wrapper/vst2_wrap.c` (local copy with one-click-one-unit wheel fix)
- `tools/` — `gen_params.py`, `skin.py`, `field_check.py`, `skin_post.py`, `tile.py`, `compile_arm64.sh`
- `art/` — tile and fonts
- `test/` — `engine_test.c`, `golden_test.c`, `run_tests.sh`, `host_test.sh`, `arm_sim.sh`
- `third_party/` — vendored framework (`mpc-vst-plugins`) and Airwindows code (MIT)
- `docs/NOTES.md` — cross-release design decisions

## License

MIT — see `LICENSE`. Vendored components keep their own licenses (see `third_party/` and `art/fonts/`).
