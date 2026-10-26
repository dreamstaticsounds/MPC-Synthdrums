# Testing — MPC Synthdrums

Two layers: the offline suite that runs on any Linux box (below), and the on-device smoke test on the Akai Force
(armv7, MPC OS). The device steps are **pending**: they need the Force's IP and the user at the unit, so no ssh/scp
was run for this revision.

**Local wrapper copy.** `src/wrapper/vst2_wrap.c` is the stock framework wrapper with one deliberate change — a
data-wheel click (1% of a parameter's range) moves **one unit** on every whole-number control. The stock rounding
moved 1.27 units on a 0..127 knob but one on a 48-step knob, which felt inconsistent on hardware. The vendored
`third_party/mpc-vst-plugins/` stays byte-identical; `build.sh` and `test/host_test.sh` link the local copy, and the
framework host test (wheel clicks, Q-Link events, sweeps, jumps) passes against it. Ported from the `main` line's
1.0.0 fix (commit `c349111`). **1.1.0 patched no vendored tool at all** (no `host_test.c`/`bench.c` changes: the host
test is `NPARAMS`-driven and the `-w` bench preset matches params *by name*, and no param name changed).

## Offline results

All commands run from the repo root. Re-run on 2026-10-25 for the 1.1.0 release.

| Check | Command | Result | Date | Task |
|---|---|---|---|---|
| Unit suite (engine, ASan/UBSan) | `test/run_tests.sh` | **PASSED**, exit 0 — 282 executed checks, no sanitizer report (the suite prints only `PASSED`; 282 measured with an instrumented throwaway copy of `test/engine_test.c`) | 2026-10-25 | 2–8 |
| Golden verify (inside the unit suite) | `test/golden_test test/goldens/fx-sound.txt` | **PASSED** — all three FNV hashes match the fixture captured at `972c519`; the flat master EQ (bypass at gain 0) keeps the hashed stream bit-identical to 1.0.3 | 2026-10-25 | 4 |
| Host test (framework VST2 host) | `test/host_test.sh` | **PASSED**, exit 0 — 77/77 `ok`; state chunk 2624 B (was 2596 B — the `meq=` line adds 26 B; < 8000 guard is in the unit suite); the popup's `__open` flag is **not** saved in the chunk; `fx_reset` is absent from the chunk; two-thread screen/audio check included | 2026-10-25 | 2, 3 |
| Skin check | the `skin_check.py` gate inside `build.sh` (same filter as 1.0.3: only `TOUCH` lines naming popup rows exempt) | exit 0, **no hard findings**; by-design popup `TOUCH` lines unchanged; built skin: **7 pages, 317 definitions, 370 pictures** | 2026-10-25 | 6, 7 |
| Picker fields | `python3 tools/field_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums"` | **0 failures** — nine `sdField_` defs (LFO pickers, unchanged by 1.1.0) | 2026-10-25 | 6 |
| Param/layout checks | `python3 tools/gen_params.py --check && python3 tools/skin.py --check` | exit 0, **silent** — 130 parameters + 9 hidden `__open` popup params (params.h: **139**); layout matches `layout.conf` (7 tabs incl. `[tab MASTER]`) | 2026-10-25 | 2, 3, 6, 7 |
| armhf build gates | `nm -D build/arm/synthdrums.so \| awk '$2=="T"'`; `readelf -V` max GLIBC; `file`; `version.xml` | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (≤ 2.31); ELF 32-bit LSB ARM EABI5, **80 720 B** (was 72 172 B); `version.xml` **1.1.0.0** | 2026-10-25 | 1, 9 |
| aarch64 build gates | same three on `build/arm64/synthdrums.so` | **1** exported `T` symbol; **max GLIBC = 2.27**; ELF 64-bit LSB ARM aarch64, **104 600 B** (was 92 312 B); `version.xml` **1.1.0.0** | 2026-10-25 | 9 |
| aarch64 engine test under qemu | `test/arm_sim.sh` | **PASSED**, exit 0 — same green suite as x86 (binfmt had to be re-registered first: `docker run --privileged --rm tonistiigi/binfmt --install arm64` — it does not survive docker/host restarts) | 2026-10-25 | 9 |
| CPU bench, x86 (relative only) | `bash third_party/mpc-vst-plugins/tools/bench.sh build/host/synthdrums.so local -j` and `… -w -j`, against a `6ba97b4` (1.0.3) baseline worktree cut the same way | Default: **PASS**, worst p99 **5.8–6.3 %**, worst block **33.6–33.7 %** across runs (baseline 4.9 / 33.8 — the flat master EQ is free, as promised). Worst case (`-w`, final-binary run last): p99 **28.4–29.7 %** (final binary **28.8 %**; baseline **30.0 %** — ≤ ~2× criterion met at ~0.96×), worst block **91.1–100.9 %** across runs (final binary **92.7 %**; baseline **96.5 %** — single-block jitter straddling baseline; the master EQ's gains are not in the `-w` preset, so it bypasses). The bench's own absolute bands (PASS ≤15/≤50) fail **both** builds on `-w`, exactly as in 1.0.3; the device number is the official gate. | 2026-10-25 | 9 |

`tools/skin_post.py` (wired via `vst.json "skin_post"`) stamps the skin's `version.xml` with the release version.

### Build status by architecture

| Arch | Status |
|---|---|
| armv7 (armhf) | **Compiled** by `./build.sh` (Zig, glibc 2.31 target; max required 2.27). Runtime **pending** the device step — no armv7 emulator exists on this machine. |
| aarch64 | **Compiled** by `ARCH=aarch64 ./build.sh` (Docker `mm303:aarch64-audio`, native GCC 11.5, glibc 2.31; max required 2.27); engine suite green under qemu (`test/arm_sim.sh`). Runtime **pending** on-device (Gen2: MPC Live/One/X/Key 3). |

Release packaging note: `release.py` reports **MPC OS compatibility: 3.x only** for this build, because the skin's
`TUI.json` uses actions/Button version 2 (MPC OS 2.15.1 uses version 1). The armhf binary itself needs only
GLIBC 2.27, so this is a skin-format limit, not a binary limit. The Force used for the device test must run MPC OS 3.x.

## Device checklist (Akai Force, armv7) — the ten 1.1.0 acceptance items

Status: **pending — awaiting the user's Force.** Carried-over checks from 1.0.3 (FM drone, delay per-side sync,
tempo-synced stutter, EQ rows, voice-select, project reload) stay in the acceptance pass below where they changed.

| # | Check | Result |
|---|---|---|
| 1 | **Panel relabels**: FX page reads DETUNE / TIME L / TIME R / FEEDBACK L / FEEDBACK R / MIX, with SYNC L and SYNC R above their knobs and the FX RESET key; OSC page reads OSC/NOISE, DECAY, SHAPE, X-MOD and COLOUR, ATTACK, DECAY, SHAPE; PITCH reads PITCH, TRACK, ATTACK, DECAY, SHAPE, AMOUNT; FILTER reads CUTOFF / RES / ENV DEPTH / DECAY / Velocity in **both** filter branches (the CTRL page keeps its VEL→ labels). | |
| 2 | **Gated Verb swap**: LENGTH sits left of MIX in GATED VERB (was WET MIX first); the FX Q-Links follow (bank 1 ends LENGTH, bank 2 starts MIX); pressing FX RESET still restores all 19 FX params of the selected voice. | |
| 3 | **MASTER tab exists** (7 tabs, between LFO and CTRL) and is reachable on the device. | |
| 4 | **Mixer**: fader N and PAN N drive **voice N regardless of the selected voice**; moving a fader moves the selected voice's VOL/PAN automation readback; readouts match the knob/fader. | |
| 5 | **Mixer follows patch ops**: COPY / PASTE / INIT VOICE move the selected voice's fader and pan with it; KIT INIT returns all faders to 100 and pans to centre. | |
| 6 | **Master EQ sounds**: HIGH shelf boosts/cuts the top, LOW shelf the bottom, the middle band rings like the Filter-tab EQ; the freq readouts show Hz/kHz; **flat defaults sound identical to 1.0.3** (A/B an old project). | |
| 7 | **KIT INIT resets the master EQ** to flat (64 / 0 / 64) — the author's decision for this release. | |
| 8 | **CTRL page**: no OUTPUT panel; KIT / CHOKE / ABOUT widened and balanced; ABOUT reads VERSION 1.1.0. | |
| 9 | **Instruments-browser tile**: violet/black (no orange left — the tile regenerates from the 1.0.3 theme). | |
| 10 | **Chunk compatibility**: a 1.0.3 project loads (flat master EQ, mixer faders show the saved per-voice vol/pan, chunk now 2624 B); save/reload on 1.1.0 round-trips; **old projects sound unchanged** (the offline goldens pin this). | |

Carried over from 1.0.3 and still to be spot-checked on the same unit: FM has no drone when the amp env dies; the
stutter rate follows MPC's tempo; the EQ shows one row per band; a pad hit selects that voice; all 12 voices play
across the pad octave; dropdowns stay open until picked; project save/reload brings every voice back.

## How to run on the device

**1. Install.** Two release zips are built offline — pick the device's generation (the installer refuses the wrong
one, and reads the arch from the `.so` itself):

```sh
dist/MPC-Synthdrums-1.1.0-mpc-armv7.zip      # Gen1 (MPC Force, Live/One/X/Key 1-2)
dist/MPC-Synthdrums-1.1.0-mpc-aarch64.zip    # Gen2 (MPC Live/One/X/Key 3, MPC 500)
```

The zips were cut on 2026-10-25 (both verified: `unzip -t` clean, 386 files, 5.6 MB each; armv7 carries the ELF
32-bit EABI5 `.so` 80 720 B, aarch64 the ELF 64-bit `.so` 104 600 B; `version.xml` **1.1.0.0**). Re-cut on request
with `release.py` (the armv7 run uses `build/arm/synthdrums.so`, the aarch64 run `build/arm64/synthdrums.so`,
everything else identical):

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.1.0 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license GPL-3.0-or-later \
  -o dist
```

`dist/` is gitignored, so the zips are build artifacts, not commits.

Scripted install (the zip's own installer; it stops and restarts MPC, so save the project first):

```sh
scp dist/MPC-Synthdrums-1.1.0-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.1.0-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.1.0/install.sh"
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

* per-voice edits survive a project save/reload (item 10) — the state chunk serializes to **2624 B** (the 1.0.3
  chunk was 2596 B; the appended `meq=` line is the difference), a wide margin under the wrapper's 8192 B buffer;
* `display_rev` moves the MPC screen to the new voice's values after a pad hit, and a mixer move on the selected
  voice bumps it (the wrapper then automates the old VOL/PAN readbacks);
* bench p99 ≤ 15 % / worst block ≤ 50 % is PASS (verdict table in `third_party/mpc-vst-plugins/docs/BENCH.md`); the
  x86 numbers for 1.1.0 are p99 **5.8–6.3 %** / worst **33.6–33.7 %** (defaults) and p99 **28.4–29.7 %** / worst
  **91.1–100.9 %** (`-w`; final-binary run 28.8 / 92.7), against the 6ba97b4 baseline's 4.9 / 33.8 and 30.0 /
  96.5. The device numbers are the release gate.
