# Testing — MPC Synthdrums

Two layers: the offline suite that runs on any Linux box (below), and the on-device smoke test on the Akai Force
(armv7, MPC OS). The device steps are **pending**: they need the Force's IP and the user at the unit, so no ssh/scp
was run for this revision.

## Offline results

All commands run from the repo root. Re-run on 2026-10-22 for the 1.0.1 release.

| Check | Command | Result | Date | Task |
|---|---|---|---|---|
| Unit suite (engine, ASan/UBSan) | `test/run_tests.sh` | **PASSED**, exit 0 — 134 executed checks, no sanitizer report (the suite prints only `PASSED`; the 134 is the executed `CHECK` count, measured with an instrumented throwaway copy of `test/engine_test.c`) | 2026-10-22 | 1–9 |
| Host test (framework VST2 host) | `test/host_test.sh` | **PASSED**, exit 0 — 35/35 `ok`; state chunk 2236 B (< 8000 guard is in the unit suite); the popup's `__open` flag is **not** saved in the chunk (new check); two-thread screen/audio check included | 2026-10-22 | 14 |
| Skin check | filter as in `build.sh`: `skin_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums" 2>err \| grep -v "TOUCH.*popup lfo"` (stderr checked for a crash; only `TOUCH` lines naming popup rows are exempt) | exit 0, **no hard findings**; 188 `TOUCH` lines naming `popup <key> <option>` rows are by-design (an open picker list covers the page and takes the touch — the framework itself only warns) and are filtered; built skin: 6 pages, 277 definitions, 341 pictures | 2026-10-22 | 14 |
| Param/layout checks | `python3 tools/gen_params.py --check && python3 tools/skin.py --check` | exit 0, **silent** — 86 parameters + 9 hidden `__open` popup params (params.h: 95), layout matches `layout.conf` | 2026-10-22 | 1, 14 |
| armhf build gates | `nm -D build/arm/synthdrums.so \| awk '$2=="T"'`; `python3 -c '…catalog_check.max_glibc("build/arm/synthdrums.so")'`; `file build/arm/synthdrums.so`; package listing | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (≤ 2.31; numeric check — not the lexicographic `sort -u \| tail -1`, which misleadingly prints `GLIBC_2.4`); ELF 32-bit LSB ARM EABI5, hard-float, stripped, 62 932 B; package = `Plugin Skins/` + `Presets/` + `version.xml` (**1.0.1.0**) + `synthdrums.so` | 2026-10-22 | 15, review fixes |
| aarch64 build gates | same four on `build/arm64/synthdrums.so` | **1** exported `T` symbol; **max GLIBC = 2.27**; ELF 64-bit LSB ARM aarch64, stripped, 80 016 B; same package layout, `version.xml` **1.0.1.0** | 2026-10-22 | 15, review fixes |
| aarch64 engine test under qemu | `test/arm_sim.sh` | **PASSED**, exit 0 — same green suite as x86 (static-musl aarch64 build via Zig, qemu-aarch64 via binfmt; after `docker run --privileged --rm tonistiigi/binfmt --install arm64` to re-register the handler) | 2026-10-22 | 15 |
| CPU bench, x86 (relative only) | `bash third_party/mpc-vst-plugins/tools/bench.sh build/host/synthdrums.so local -j` | **PASS** — 95 params, worst p99 4.3 %, worst block 21.0 %, threads 0.0 % of a core (budget 2902 µs/block; verdict table in `third_party/mpc-vst-plugins/docs/BENCH.md`). x86 numbers are regression baselines only; the device number is the pending on-device bench | 2026-10-22 | 15 |

`tools/skin_post.py` (new, wired via `vst.json "skin_post"`) stamps the skin's `version.xml` with the release
version: the framework's `write_skin` hardcodes `1.0.0.0` there, which was right for 1.0.0 and stale since.

### Build status by architecture

| Arch | Status |
|---|---|
| armv7 (armhf) | **Compiled** by `./build.sh` (Zig, glibc 2.31 target; max required 2.27). Runtime **pending** the device step — no armv7 emulator exists on this machine. |
| aarch64 | **Compiled** by `ARCH=aarch64 ./build.sh` (Docker `mm303:aarch64-audio`, native GCC 11.5, glibc 2.31; max required 2.27); engine suite green under qemu (`test/arm_sim.sh`). Runtime **pending** on-device (Gen2: MPC Live/One/X/Key 3). |

Release packaging note: `release.py` reports **MPC OS compatibility: 3.x only** for this build, because the skin's
`TUI.json` uses actions/Button version 2 (MPC OS 2.15.1 uses version 1). The armhf binary itself needs only
GLIBC 2.27, so this is a skin-format limit, not a binary limit. The Force used for the device test must run MPC OS 3.x.

## Device checklist (Akai Force, armv7)

Status: **pending — awaiting the user's Force IP.** The 1.0.1 list; results to be filled in on the device.

| # | Check | Result |
|---|---|---|
| 1 | Plugin loads, skin renders on all **6 tabs** (OSC · PITCH · FILTER · FX · LFO · CTRL); the GLB tab of 1.0.0 is now CTRL. | |
| 2 | **A pad hit selects that voice** for editing (the old VOICE strip is gone): hit a pad, the CTRL page's voice readout follows, an edit changes that voice; the ◀ ▶ arrows still browse without playing. | |
| 3 | Play all 12 voices across one pad octave (C2..B2 at base 36); change BASE NOTE, confirm the mapping moves. | |
| 4 | FILTER page: osc + noise filters (LP/BP/HP, cutoff/res/env depth/decay/vel each), the 3 peaking EQs show real Hz/kHz, stutter knobs respond. | |
| 5 | FX page: dist/ring/FM/comb/gated-reverb engage in chain order, delay last (wet stereo after pan); ring mod off at 0, comb off at 0. | |
| 6 | **Popups**: tap an LFO WAVE/DEST/DIV field — the list opens; pick an option — it closes and shows the choice; a Q-Link nudge of an open list's value steps the value (list behavior per docs/NOTES.md). | |
| 7 | LFOs: sync ON divisions follow the MPC's tempo (change tempo, the synced rate tracks); retrig ON re-triggers with each hit; depth bidirectional around the parameter's center. | |
| 8 | Q-Links on each tab and both banks; automation write of one edit param; CC/NRPN from an external controller. | |
| 9 | Save the project, reload: **all 12 voices' edits come back** (state chunk 2236 B, well under the 8192 guard). | |
| 10 | Load a **1.0.0 project**: it loads and plays — the sound is audibly different by design (1.0.1 reworks osc_shape, the filters, EQ and FX). Params map by their stable indices (append-only). | |
| 11 | CPU: `third_party/mpc-vst-plugins/tools/bench.sh build/arm/synthdrums.so <ip> -v 1,4,8,12,16 -j` with all 12 voices sounding — record µs/block here. Budget 2902 µs/block; PASS = p99 ≤ 15 % / worst ≤ 50 %. | |

## How to run on the device

**1. Install.** Two release zips are built offline — pick the device's generation (the installer refuses the wrong
one, and reads the arch from the `.so` itself):

```sh
dist/MPC-Synthdrums-1.0.1-mpc-armv7.zip      # Gen1 (MPC Force, Live/One/X/Key 1-2)
dist/MPC-Synthdrums-1.0.1-mpc-aarch64.zip    # Gen2 (MPC Live/One/X/Key 3, MPC 500)
```

The zips are **not cut yet** — `release.py` runs only when a release is asked for. Each is produced by
(`python3 …/release.py --help` documents the flags; the armv7 run uses `build/arm/synthdrums.so`, the aarch64 run
`build/arm64/synthdrums.so`, everything else identical):

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.0.1 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license GPL-3.0-or-later \
  -o dist
```

`dist/` is gitignored, so the zips are build artifacts, not commits.

Scripted install (the zip's own installer; it stops and restarts MPC, so save the project first):

```sh
scp dist/MPC-Synthdrums-1.0.1-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.0.1-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.0.1/install.sh"
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

* per-voice edits survive a project save/reload (item 9) — the state chunk currently serializes to 2236 B, so there
  is a wide margin under the wrapper's 8192 B buffer;
* `display_rev` moves the MPC screen to the new voice's values after a pad hit (item 2);
* bench p99 ≤ 15% / worst block ≤ 50% is PASS (Verdict table in `docs/BENCH.md`); the x86 baseline for 1.0.1 is
  p99 4.3% / worst 21.0%.
