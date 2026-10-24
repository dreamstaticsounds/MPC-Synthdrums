# Testing — MPC Synthdrums

Two layers: the offline suite that runs on any Linux box (below), and the on-device smoke test on the Akai Force
(armv7, MPC OS). The device steps are **pending**: they need the Force's IP and the user at the unit, so no ssh/scp
was run for this revision.

**Local wrapper copy.** `src/wrapper/vst2_wrap.c` is the stock framework wrapper with one deliberate change — a
data-wheel click (1% of a parameter's range) moves **one unit** on every whole-number control. The stock rounding
moved 1.27 units on a 0..127 knob but one on a 48-step knob, which felt inconsistent on hardware. The vendored
`third_party/mpc-vst-plugins/` stays byte-identical; `build.sh` and `test/host_test.sh` link the local copy, and the
framework host test (wheel clicks, Q-Link events, sweeps, jumps) passes against it. Ported from the `main` line's
1.0.0 fix (commit `c349111`).

## Offline results

All commands run from the repo root. Re-run on 2026-10-23 for the 1.0.3 release.

| Check | Command | Result | Date | Task |
|---|---|---|---|---|
| Unit suite (engine, ASan/UBSan) | `test/run_tests.sh` | **PASSED**, exit 0 — 234 executed checks, no sanitizer report (the suite prints only `PASSED`; 234 measured with an instrumented throwaway copy of `test/engine_test.c`) | 2026-10-23 | 1–10 |
| Host test (framework VST2 host) | `test/host_test.sh` | **PASSED**, exit 0 — 77/77 `ok`; state chunk 2596 B (< 8000 guard is in the unit suite); the popup's `__open` flag is **not** saved in the chunk; `fx_reset` is absent from the chunk; two-thread screen/audio check included | 2026-10-23 | 4, 8 |
| Skin check | filter as in `build.sh`: `skin_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums" 2>err \| grep -v "TOUCH.*popup lfo"` (stderr checked for a crash; only `TOUCH` lines naming popup rows are exempt) | exit 0, **no hard findings**; 188 `TOUCH` lines naming `popup <key> <option>` rows are by-design (an open picker list covers the page and takes the touch); built skin: 6 pages, 306 definitions, 370 pictures | 2026-10-23 | 7–9 |
| Picker fields | `python3 tools/field_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums"` | **0 failures** — nine `sdField_` defs, each two Toggle Switch actions + Focus/Label@Text (no Button), Data→`__open`, Text→the enum, no old field defs/kids | 2026-10-23 | 8 |
| Param/layout checks | `python3 tools/gen_params.py --check && python3 tools/skin.py --check` | exit 0, **silent** — 97 parameters + 9 hidden `__open` popup params (params.h: 106), layout matches `layout.conf` | 2026-10-23 | 2, 7 |
| armhf build gates | `nm -D build/arm/synthdrums.so \| awk '$2=="T"'`; `catalog_check.max_glibc`; `file`; package listing | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (≤ 2.31); ELF 32-bit LSB ARM EABI5, hard-float, stripped, **72 172 B**; `version.xml` **1.0.3.0** | 2026-10-23 | 10 |
| aarch64 build gates | same four on `build/arm64/synthdrums.so` | **1** exported `T` symbol; **max GLIBC = 2.27**; ELF 64-bit LSB ARM aarch64, stripped, **92 312 B**; `version.xml` **1.0.3.0** | 2026-10-23 | 10 |
| aarch64 engine test under qemu | `test/arm_sim.sh` | **PASSED**, exit 0 — same green suite as x86 (static-musl aarch64 build via Zig, qemu-aarch64 via binfmt) | 2026-10-23 | 10 |
| CPU bench, x86 (relative only) | `bash third_party/mpc-vst-plugins/tools/bench.sh build/host/synthdrums.so local -j` and `… -w -j` | Default: **PASS**, worst p99 **5.4–6.7 %**, worst block **36.9–44.4 %**. Worst case (`-w`: 12 voices, every FX engaged, both delay feedbacks): p99 **26.6 %**, worst **93.0 %** after the verb cap (1.0.2 baseline with the same preset: p99 16.2 %, worst 59.0 %). The release criterion is ≤ ~2× baseline p99 and < 100 % worst — both met. History: 488 taps measured p99 **70.8 %** / worst **133.6 %** → capped at 256 (40.1 / 114.9) → capped at **128** (26.6 / 93.0). The bench's own absolute bands are stricter (PASS ≤15/≤50), which the 1.0.2 baseline's worst case also misses; the device number is the official gate. | 2026-10-23 | 10 |

`tools/skin_post.py` (wired via `vst.json "skin_post"`) stamps the skin's `version.xml` with the release version.

### Build status by architecture

| Arch | Status |
|---|---|
| armv7 (armhf) | **Compiled** by `./build.sh` (Zig, glibc 2.31 target; max required 2.27). Runtime **pending** the device step — no armv7 emulator exists on this machine. |
| aarch64 | **Compiled** by `ARCH=aarch64 ./build.sh` (Docker `mm303:aarch64-audio`, native GCC 11.5, glibc 2.31; max required 2.27); engine suite green under qemu (`test/arm_sim.sh`). Runtime **pending** on-device (Gen2: MPC Live/One/X/Key 3). |

Release packaging note: `release.py` reports **MPC OS compatibility: 3.x only** for this build, because the skin's
`TUI.json` uses actions/Button version 2 (MPC OS 2.15.1 uses version 1). The armhf binary itself needs only
GLIBC 2.27, so this is a skin-format limit, not a binary limit. The Force used for the device test must run MPC OS 3.x.

## Device checklist (Akai Force, armv7) — the ten 1.0.3 acceptance items

Status: **pending — awaiting the user's Force.** Carried-over checks from 1.0.2 (FM drone, delay per-side sync,
tempo-synced stutter, EQ rows, voice-select, project reload) stay in the acceptance pass below where they changed.

| # | Check | Result |
|---|---|---|
| 1 | **L/R SYNC above their knobs**: the FX page shows L SYNC over DELAY L and R SYNC over DELAY R (not under the title tab); each pair still toggles OFF/ON and ON turns that knob into a division selector. | |
| 2 | **Independent feedback**: DELAY L FB and DELAY R FB knobs replace DELAY FB; at equal values the sound is the 1.0.2 delay (goldens pin it); set one to 0 and that side's feedback stops while the other still repeats. | |
| 3 | **FX RESET**: pressing RESET restores all 19 FX parameters of the selected voice to their defaults; the changed knobs update on screen and via automation; the key springs back; a saved project does not carry the press. | |
| 4 | **GVERB LENGTH**: 0 = the 1.0.2 single ~142 ms tap; up = more taps, a longer tail and the readout shows the tap count (0→1 … 64→33 … 127→128); the voice still frees after the tail, no denormals. | |
| 5 | **No TONE knob**: the DIST panel has DRIVE only; an old project that used DIST TONE still sounds with that tone (the parameter is applied, just not shown). | |
| 6 | **Three MIX knobs** (RING MOD, FM, COMB): 127 = the 1.0.2 pure-wet sound, 0 = exact dry, in between blends; engagement 0 bypasses regardless of mix (an old chunk with the module at 0 stays dry). | |
| 7 | **Dropdowns stay open**: tap anywhere on an LFO WAVE/DEST/DIV field — the list opens and stays open until a pick (no instant open-then-close); tapping the field again closes it. | |
| 8 | **Violet everywhere**: all six pages use the medium-dark violet (no tan/yellow left); tab titles legible (near-white on violet). | |
| 9 | **Chunk restore across versions**: a 1.0.2 project's 2356 B chunk loads into 1.0.3 (state chunk is now 2596 B; appended params default: mixes 127, L/R FB = the old fb, LENGTH 1). | |
| 10 | **Old projects sound unchanged** except the deliberate fb split: 1.0.0/1.0.1/1.0.2 projects load; equal L/R FB reproduces the old delay; verb length 1 = the old tap; the three mixes at 127 = the old pure wet. | |

Carried over from 1.0.2 and still to be spot-checked on the same unit: FM has no drone when the amp env dies; the
stutter rate follows MPC's tempo; the EQ shows one row per band; a pad hit selects that voice; all 12 voices play
across the pad octave; Q-Links on both FX banks (bank 1: drive/ring/ring mix/FM/FM mix/comb/comb mix/verb; bank 2:
verb length/width/detune/L/R/L FB/R FB/wet); project save/reload brings every voice back.

## How to run on the device

**1. Install.** Two release zips are built offline — pick the device's generation (the installer refuses the wrong
one, and reads the arch from the `.so` itself):

```sh
dist/MPC-Synthdrums-1.0.3-mpc-armv7.zip      # Gen1 (MPC Force, Live/One/X/Key 1-2)
dist/MPC-Synthdrums-1.0.3-mpc-aarch64.zip    # Gen2 (MPC Live/One/X/Key 3, MPC 500)
```

The zips were cut on 2026-10-23 (both verified: `unzip -t` clean, 386 files, 5.3 MB each; armv7 carries the ELF
32-bit EABI5 `.so` 72 172 B, aarch64 the ELF 64-bit `.so` 92 312 B; `version.xml` **1.0.3.0**). Re-cut on request
with `release.py` (the armv7 run uses `build/arm/synthdrums.so`, the aarch64 run `build/arm64/synthdrums.so`,
everything else identical):

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.0.3 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license GPL-3.0-or-later \
  -o dist
```

`dist/` is gitignored, so the zips are build artifacts, not commits.

Scripted install (the zip's own installer; it stops and restarts MPC, so save the project first):

```sh
scp dist/MPC-Synthdrums-1.0.3-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.0.3-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.0.3/install.sh"
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
third_party/mpc-vst-plugins/tools/bench.sh build/arm/synthdrums.so <ip> -w -j | tee build/bench-worst.txt
```

The JSON line records p99/max as a percentage of one 2902 µs block; `release.py --bench build/bench.txt` puts it into
a later release's `INSTALL.md`. The device is pinned to core 1 by `bench.sh`, so MPC keeps running normally.

## Expected numbers to record

* per-voice edits survive a project save/reload (item 9/10) — the state chunk currently serializes to 2596 B, so there
  is a wide margin under the wrapper's 8192 B buffer;
* `display_rev` moves the MPC screen to the new voice's values after a pad hit;
* bench p99 ≤ 15 % / worst block ≤ 50 % is PASS (verdict table in `third_party/mpc-vst-plugins/docs/BENCH.md`); the
  x86 numbers for 1.0.3 are p99 5.4–6.7 % / worst block 36.9–44.4 % (defaults) and p99 26.6 % / worst 93.0 % (the
  `-w` worst-case preset at the 128-tap verb cap). The device numbers are the release gate.
