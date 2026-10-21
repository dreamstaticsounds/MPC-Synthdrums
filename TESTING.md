# Testing — MPC Synthdrums

Two layers: the offline suite that runs on any Linux box (below), and the on-device smoke test on the Akai Force
(armv7, MPC OS). The device steps are **pending**: they need the Force's IP and the user at the unit, so no ssh/scp
was run for this revision.

## Offline results

All commands run from the repo root. Re-run on 2026-10-21 for this file.

| Check | Command | Result | Date | Task |
|---|---|---|---|---|
| Unit suite (engine, ASan/UBSan) | `test/run_tests.sh` | **PASSED**, exit 0 — 68 checks, no sanitizer report (the suite prints only `PASSED`; the 68 is the executed `CHECK` count, measured with an instrumented throwaway copy of `test/engine_test.c`) | 2026-10-21 | 2–5 |
| Host test (framework VST2 host) | `test/host_test.sh` | **PASSED**, exit 0 — 30/30 `ok`; state chunk 1108 B (< 8000 guard is in the unit suite); two-thread screen/audio check included | 2026-10-21 | 6 |
| Skin check | `python3 third_party/mpc-vst-plugins/tools/skin_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums"` | exit 0, **no findings** (no TOUCH/EDGE/QLINK/OPTS); built skin: 5 pages, 72 definitions, 62 pictures | 2026-10-21 | 7 |
| armhf build gates | `nm -D build/arm/synthdrums.so \| awk '$2=="T"'`; `python3 -c '…catalog_check.max_glibc("build/arm/synthdrums.so")'`; `file build/arm/synthdrums.so`; `find "build/package/Dream Static Sounds - VST - MPC Synthdrums" -maxdepth 1` | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (≤ 2.31; numeric check — not the lexicographic `sort -u \| tail -1`, which misleadingly prints `GLIBC_2.4`); ELF 32-bit LSB ARM EABI5, hard-float, stripped; package = `Plugin Skins/` + `Presets/` + `version.xml` + `synthdrums.so` (37 500 B) | 2026-10-21 | 7 |
| aarch64 build gates | `nm -D build/arm64/synthdrums.so \| awk '$2=="T"'`; `python3 -c '…catalog_check.max_glibc("build/arm64/synthdrums.so")'`; `file build/arm64/synthdrums.so`; `find "build/package-aarch64/Dream Static Sounds - VST - MPC Synthdrums" -maxdepth 1` | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (same numeric check); ELF 64-bit LSB shared object, ARM aarch64, stripped; package = `Plugin Skins/` + `Presets/` + `version.xml` + `synthdrums.so` (43 152 B) | 2026-10-21 | 9 |
| aarch64 engine test under qemu | `test/arm_sim.sh` | **PASSED**, exit 0 — same green suite as x86; static-musl aarch64 build (Zig `-target aarch64-linux-musl`), executed by qemu-aarch64 via binfmt (no ARM hardware involved) | 2026-10-21 | 9 |
| Param generator check | `python3 tools/gen_params.py --check && python3 tools/skin.py --check` | exit 0, **silent** — 42 parameters (35 edit + voice/base_note + 4 trig keys + voice_name), layout matches `layout.conf` | 2026-10-21 | 1, 7 |

### Build status by architecture

| Arch | Status |
|---|---|
| armv7 (armhf) | **Compiled** by `./build.sh` (Zig, glibc 2.31 target; max required 2.27). Runtime **pending** this task's device step — no armv7 emulator exists on this machine. |
| aarch64 | **Compiled** by `ARCH=aarch64 ./build.sh` (Docker `mm303:aarch64-audio`, native GCC 11.5, glibc 2.31; max required 2.27); engine suite green under qemu (`test/arm_sim.sh`). Runtime **pending** on-device (Gen2: MPC Live/One/X/Key 3). |

Release packaging note: `release.py` reports **MPC OS compatibility: 3.x only** for this build, because the skin's
`TUI.json` uses actions/Button version 2 (MPC OS 2.15.1 uses version 1). The armhf binary itself needs only
GLIBC 2.27, so this is a skin-format limit, not a binary limit. The Force used for the device test must run MPC OS 3.x.

## Device checklist (Akai Force, armv7)

Status: **pending — awaiting the user's Force IP.** Results to be filled in during Task 8 Steps 3–4.

| # | Check | Result |
|---|---|---|
| 1 | Plugin loads, skin renders on all 5 tabs. | |
| 2 | Play all 12 voices across one pad octave (C2..B2 at base 36); change BASE NOTE, confirm the mapping moves. | |
| 3 | Switch voices with the selector and ◀ ▶: MPC's screen shows the new voice's values (display_rev works on hardware — Review Focus #2). | |
| 4 | Edit a few params per voice, TRIG auditions, KIT INIT resets. | |
| 5 | Q-Links on each tab and bank; automation write of one edit param; CC/NRPN from an external controller. | |
| 6 | Save the project, reload: **all 12 voices' edits come back** (Review Focus #1 — the critical one). | |
| 7 | CPU: check `third_party/mpc-vst-plugins/tools/bench.sh --help` first for its current flags (GAIAXXL's build.sh invokes it as `bench.sh <so> <ip>`), then bench `build/arm/synthdrums.so` with all 12 voices sounding — record µs/block in TESTING.md. | |

Note on item 7: `bench.sh` has no `--help` (it reads `<so> <ip>` positionally; `--help` exits with
`$2: unbound variable`). Its flags, per `third_party/mpc-vst-plugins/docs/BENCH.md`, are `-s <secs>`,
`-v <voice list>`, `-p` (sweep every parameter) and `-j` (one JSON line).

## How to run on the device

**1. Install.** Two release zips are built offline — pick the device's generation (the installer refuses the wrong
one, and reads the arch from the `.so` itself):

```sh
dist/MPC-Synthdrums-1.0.0-mpc-armv7.zip      # Gen1 (MPC Force, Live/One/X/Key 1-2): 78 files, 2.1 MB
                                             # sha256 fc840b6663f25d42417e1ad99b8756ee6418d35180348fa98b09c6e552227485
dist/MPC-Synthdrums-1.0.0-mpc-aarch64.zip    # Gen2 (MPC Live/One/X/Key 3, MPC 500): 78 files, 2.1 MB
                                             # sha256 cd46fd80884c55ad96a8380d6ab66d25bb912885d7eedbb72c049d780c7f8764
```

Each was produced by (`python3 …/release.py --help` documents the flags; the armv7 run uses `build/arm/synthdrums.so`,
the aarch64 run `build/arm64/synthdrums.so`, everything else identical):

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.0.0 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license GPL-3.0-or-later \
  -o dist
```

`dist/` is gitignored, so the zips are build artifacts, not commits.

Scripted install (the zip's own installer; it stops and restarts MPC, so save the project first):

```sh
scp dist/MPC-Synthdrums-1.0.0-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.0.0-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.0.0/install.sh"
```

The installer registers `pluginList-arm` on Gen1 and `pluginList-arm-64bit` on Gen2 (from the `.so`'s ELF arch), so no
manual settings edit is needed either way.

If the device has no `unzip`, unzip locally and copy the folder, then run its `install.sh` the same way.

Manual fallback (no installer, no plugin-list edit):

```sh
scp -r "build/package/Dream Static Sounds - VST - MPC Synthdrums" root@<ip>:/sdcard/Synths/
```

Then load **MPC Synthdrums** on a track from the plugin browser (instrument plugins) and walk the checklist above.

**2. Bench.** With all 12 voices sounding, from the repo root:

```sh
third_party/mpc-vst-plugins/tools/bench.sh build/arm/synthdrums.so <ip> -v 1,4,8,12,16 -j | tee build/bench.txt
```

The JSON line records p99/max as a percentage of one 2902 µs block; `release.py --bench build/bench.txt` puts it into
a later release's `INSTALL.md`. The device is pinned to core 1 by `bench.sh`, so MPC keeps running normally.

## Expected numbers to record

* per-voice edits survive a project save/reload (item 6) — the state chunk currently serializes to 1108 B, so there
  is a wide margin under the wrapper's 8192 B buffer;
* `display_rev` moves the MPC screen to the new voice's values (item 3);
* bench p99 ≤ 15% / worst block ≤ 50% is PASS (Verdict table in `docs/BENCH.md`).
