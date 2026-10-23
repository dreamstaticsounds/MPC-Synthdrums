# Testing — MPC Synthdrums

Two layers: the offline suite that runs on any Linux box (below), and the on-device smoke test on the Akai Force
(armv7, MPC OS). The device steps are **pending**: they need the Force's IP and the user at the unit, so no ssh/scp
was run for this revision.

## Offline results

All commands run from the repo root. Re-run on 2026-10-22 for the 1.0.2 release.

| Check | Command | Result | Date | Task |
|---|---|---|---|---|
| Unit suite (engine, ASan/UBSan) | `test/run_tests.sh` | **PASSED**, exit 0 — 180 executed checks, no sanitizer report (the suite prints only `PASSED`; the 180 is the executed `CHECK` count, measured with an instrumented throwaway copy of `test/engine_test.c`) | 2026-10-22 | 1–13 |
| Host test (framework VST2 host) | `test/host_test.sh` | **PASSED**, exit 0 — 35/35 `ok`; state chunk 2356 B (< 8000 guard is in the unit suite); the popup's `__open` flag is **not** saved in the chunk (new check); two-thread screen/audio check included | 2026-10-22 | 14 |
| Skin check | filter as in `build.sh`: `skin_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums" 2>err \| grep -v "TOUCH.*popup lfo"` (stderr checked for a crash; only `TOUCH` lines naming popup rows are exempt) | exit 0, **no hard findings**; 188 `TOUCH` lines naming `popup <key> <option>` rows are by-design (an open picker list covers the page and takes the touch — the framework itself only warns) and are filtered; built skin: 6 pages, 288 definitions, 354 pictures | 2026-10-22 | 14 |
| Param/layout checks | `python3 tools/gen_params.py --check && python3 tools/skin.py --check` | exit 0, **silent** — 90 parameters + 9 hidden `__open` popup params (params.h: 99), layout matches `layout.conf` | 2026-10-22 | 1, 14 |
| armhf build gates | `nm -D build/arm/synthdrums.so \| awk '$2=="T"'`; `python3 -c '…catalog_check.max_glibc("build/arm/synthdrums.so")'`; `file build/arm/synthdrums.so`; package listing | **1** exported `T` symbol (`VSTPluginMain`); **max GLIBC = 2.27** (≤ 2.31; numeric check — not the lexicographic `sort -u \| tail -1`, which misleadingly prints `GLIBC_2.4`); ELF 32-bit LSB ARM EABI5, hard-float, stripped, 65 192 B; package = `Plugin Skins/` + `Presets/` + `version.xml` (**1.0.2.0**) + `synthdrums.so` | 2026-10-22 | 15, review fixes |
| aarch64 build gates | same four on `build/arm64/synthdrums.so` | **1** exported `T` symbol; **max GLIBC = 2.27**; ELF 64-bit LSB ARM aarch64, stripped, 80 016 B; same package layout, `version.xml` **1.0.2.0** | 2026-10-22 | 15, review fixes |
| aarch64 engine test under qemu | `test/arm_sim.sh` | **PASSED**, exit 0 — same green suite as x86 (static-musl aarch64 build via Zig, qemu-aarch64 via binfmt; after `docker run --privileged --rm tonistiigi/binfmt --install arm64` to re-register the handler) | 2026-10-22 | 15 |
| CPU bench, x86 (relative only) | `bash third_party/mpc-vst-plugins/tools/bench.sh build/host/synthdrums.so local -j` | **PASS** — 99 params, worst p99 **6.1–6.6 %**, worst block **24.1–28.2 %** (four runs of the same binary: the spread is the machine's session noise), threads 0.0 % of a core (budget 2902 µs/block; verdict table in `third_party/mpc-vst-plugins/docs/BENCH.md`). Attribution of the delta vs 1.0.1's recorded 4.3 %/21.0 % (a prior session): a stub-verb A/B build scores 6.2 %/22.8 % — the vendored BA3 verb itself costs **~1.4 pp of worst block and ~0 p99** (its ring wrap was also changed from `%` to a compare for the hot path); the p99 delta is not the verb. Verdict PASS with >3× margin either way; the device number is the pending on-device bench | 2026-10-22 | 15 |

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

Status: **pending — awaiting the user's Force.** The 1.0.2 list: the nine new acceptance items (1–9) plus the
carried-over 1.0.1 checks; results to be filled in on the device.

| # | Check | Result |
|---|---|---|
| 1 | **FM has no drone**: set FM on with some INDEX, hold a hit until the amp env dies — the 150 Hz operator tone must die with it (1.0.2 gates the FM operator by the amp env). | |
| 2 | **LFO picker fields**: tap anywhere on an LFO WAVE/DEST/DIV field (not just the chevron) — the list opens and **stays open**; pick — it closes and shows the choice; tap the field again while open — it closes (tap-again kept by choice). No instant open-then-close on the first tap. | |
| 3 | **LFO rate reads in 0.1 Hz** (free-run: "0.0 Hz".."20.0 Hz", e.g. "4.6 Hz" at knob 100); synced still shows "SYNC". | |
| 4 | **Tab titles are bare**: the device shows OSC · PITCH · FILTER · FX · LFO · CTRL with no " 1"; FILTER and FX each appear twice (one per Q-Link bank — intended, same controls). | |
| 5 | **Stutter is tempo-synced**: STUTTER RATE shows "OFF" or a division word (1/32T .. "4 bars", 18 steps); OFF is a true bypass; the divisions follow the MPC's tempo (change tempo, the stutter rate tracks). | |
| 6 | **EQ is one row per band**: FILTER page shows EQ1/EQ2/EQ3 rows, each Freq/Gain/Q, real Hz/kHz readouts. | |
| 7 | **Gated verb is BA3 after the delay**: GVERB at 0 = dry only; up = the ~142 ms prime tap + the bright 1 kHz ring, stereo after the delay; the tail dies on its own (no 1.0.1-style hard gate) and the voice frees afterwards. | |
| 8 | **Delay per-side sync**: L SYNC/R SYNC switches; ON turns that side's DELAY knob into a division selector (same 18 divisions, no OFF step); OFF returns ms; "4 bars" at 120 BPM clamps to the 1000 ms line cap (not an 8 s wait). | |
| 9 | **Dist TONE + WIDTH**: TONE at 127 = the 1.0.1 sound (exact bypass); lower = darker after the waveshaper. WIDTH at 0 = unchanged; up widens (far side of a panned hit comes up), a center hit stays identical. Both on Q-Link bank 1 of FX with the two SYNC switches on bank 2. | |
| 10 | Plugin loads, skin renders on all **6 tabs** (OSC · PITCH · FILTER · FX · LFO · CTRL); the GLB tab of 1.0.0 is now CTRL. | |
| 11 | **A pad hit selects that voice** for editing (the old VOICE strip is gone): hit a pad, the CTRL page's voice readout follows, an edit changes that voice; the ◀ ▶ arrows still browse without playing. | |
| 12 | Play all 12 voices across one pad octave (C2..B2 at base 36); change BASE NOTE, confirm the mapping moves. | |
| 13 | FILTER page: osc + noise filters (LP/BP/HP, cutoff/res/env depth/decay/vel each) respond; the STUTTER RATE knob responds. | |
| 14 | FX page: dist(+tone)/ring/FM/comb/gverb engage in chain order, delay after pan (wet stereo), verb and WIDTH after the delay; ring mod off at 0, comb off at 0. | |
| 15 | LFOs: sync ON divisions follow the MPC's tempo; retrig ON re-triggers with each hit; depth bidirectional around the parameter's center. | |
| 16 | Q-Links on each tab and both banks (FX bank 1 = dist drive/tone, ring, FM, comb, gverb, width, delay detune; bank 2 = delay L/R/fb/wet + both sync switches); automation write of one edit param; CC/NRPN from an external controller. | |
| 17 | Save the project, reload: **all 12 voices' edits come back** (state chunk 2356 B, well under the 8192 guard). | |
| 18 | Load a **1.0.0 project** and a **1.0.1 project**: both load and play (the 1.0.0 sound differs by design; params map by their stable indices — append-only, first 35 patch indices untouched, pre-1.0.2 LFO dest indices unchanged). | |
| 19 | CPU: `third_party/mpc-vst-plugins/tools/bench.sh build/arm/synthdrums.so <ip> -v 1,4,8,12,16 -j` with all 12 voices sounding — record µs/block here. Budget 2902 µs/block; PASS = p99 ≤ 15 % / worst ≤ 50 %. | |

## How to run on the device

**1. Install.** Two release zips are built offline — pick the device's generation (the installer refuses the wrong
one, and reads the arch from the `.so` itself):

```sh
dist/MPC-Synthdrums-1.0.2-mpc-armv7.zip      # Gen1 (MPC Force, Live/One/X/Key 1-2)
dist/MPC-Synthdrums-1.0.2-mpc-aarch64.zip    # Gen2 (MPC Live/One/X/Key 3, MPC 500)
```

The zips were cut on 2026-10-22 (both verified: `unzip -t` clean, 370 files each; armv7 carries the ELF 32-bit
EABI5 `.so` 65 192 B, aarch64 the ELF 64-bit `.so` 80 016 B; `version.xml` **1.0.2.0**). Re-cut on request with
`release.py` (the armv7 run uses `build/arm/synthdrums.so`, the aarch64 run
`build/arm64/synthdrums.so`, everything else identical):

```sh
python3 third_party/mpc-vst-plugins/tools/release.py \
  --so build/arm/synthdrums.so \
  --skin "build/skin/Dream Static Sounds - VST - MPC Synthdrums" \
  --entry build/pluginlist-entry.xml \
  --version 1.0.2 \
  --about "12-voice synth drum box for MPC OS (inspired by DSK SynthDrums; not affiliated with DSK Music)." \
  --license GPL-3.0-or-later \
  -o dist
```

`dist/` is gitignored, so the zips are build artifacts, not commits.

Scripted install (the zip's own installer; it stops and restarts MPC, so save the project first):

```sh
scp dist/MPC-Synthdrums-1.0.2-mpc-<armv7|aarch64>.zip root@<ip>:/tmp/
ssh root@<ip> "cd /tmp && unzip -o MPC-Synthdrums-1.0.2-mpc-<armv7|aarch64>.zip && sh MPC-Synthdrums-1.0.2/install.sh"
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

* per-voice edits survive a project save/reload (item 17) — the state chunk currently serializes to 2356 B, so there
  is a wide margin under the wrapper's 8192 B buffer;
* `display_rev` moves the MPC screen to the new voice's values after a pad hit (item 11);
* bench p99 ≤ 15% / worst block ≤ 50% is PASS (Verdict table in `third_party/mpc-vst-plugins/docs/BENCH.md`); the
  x86 numbers for 1.0.2 are p99 6.1–6.6% / worst block 24.1–28.2% (four runs of one binary; the vendored BA3 verb
  measures ~1.4 pp of worst block via a stub A/B — see the bench row above). 1.0.1's recorded 4.3%/21.0% was a
  prior session, not a same-binary comparison.
