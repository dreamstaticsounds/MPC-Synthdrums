# MPC Synthdrums — design-decision notes

The repo's cross-release log of decisions that are easy to lose: vendored-code adaptations,
mechanism findings, and the "why" behind changes that the design docs record as facts. (Not to
be confused with `third_party/mpc-vst-plugins/docs/NOTES.md`, the vendored framework's own
notes — those document the framework's mechanisms and are cited from `tools/skin.py`.)

Each release appends a section. Newest last.

## 1.0.2 (2026-10-22)

- **Bright Ambience 3 vendored** (`third_party/airwindows-brightambience3/`, MIT): three of the
  vendor's four knobs baked — Start 13% → `start = (int)(0.13*400)+88 = 140`, Length 0 →
  `length = 1`, Feedback 0 → `feedbackAmount = 0`. What remains is ONE prime tap per channel
  through the vendor's fixed 1 kHz "figure" resonator, wet blended by the existing `gverb`
  knob. **The taps are per-channel:** `primeL[140] = 6247` (~141.7 ms), `primeR[140] = 6229`
  (~141.2 ms) — the vendor deliberately taps a different prime per channel for stereo
  decorrelation (the 1.0.2 design doc originally misread the table as "≈329 ms"; corrected
  there, ground truth is the vendored header). The 32768-long descending-`gcount` rings
  collapse to ascending float rings exactly one tap long (read-then-write = pure tap delay);
  the figure's coefficients come from the vendor's own formula (double math, baked values)
  computed once in `verb_reset` instead of per block; the denormal dither, the VST plumbing
  and the sample-rate plumbing (cycleEnd is 1 at 44.1 kHz) are dropped. `verb_reset` zeroes
  state + rings AND sets the coefficients, so per-hit reset is bit-exact.
- **The verb runs after the delay** (stereo, on the delay's wet+dry output), replacing 1.0.1's
  mono Schroeder bank that ran in the FX row before vol/pan. `fx_gverb`/`GVerbSt`/
  `GVERB_GATE_S`/`gverb_t` are gone.
- **No per-hit gate** (user decision): 1.0.1 chopped the tail ~200 ms after the hit; BA3's
  ring IS the sound (the tap arrives at ~141 ms, the figure decays over ~100 ms after it).
  Liveness is clocked instead: per-block verb input/wet peaks reset a quiet clock; the voice
  lives `VERB_HORIZON_S` (0.6 s) past the last energy — measured cause in the release ledger:
  input dead ~108 ms (noise_decay 30), tap stream ends ~249 ms, wet below the floor ~350 ms,
  voice alive ~950 ms.
- **The stutter's division grid is shared** with the delay's tempo sync: one 18-entry table
  (`SD_DIV_BEATS`/`SD_DIV_WORDS` in `voice.c`, externs in `synthdrums.h`), the stutter's
  "length" knob maps 0 = OFF + 1..18 onto it, and each delay side's knob quantizes onto the
  same 18 divisions when its SYNC switch is on (0..127 → round(v*18/127), no OFF step — the
  switch is the off). Division rates follow the engine BPM (the render call's 4th arg).
- **`shadow_skin.py` is patched** (vendored file, one line + comment): `componentName` became
  `"%s|%s|%d" % (tab["name"], title, sp)` so bare tab titles ("FILTER", "FX" — no more
  " 1"/" 2" on the device) can't collide between a tab's two Q-Link sub-pages. `tabName`
  (what the device displays) stays bare; both sub-pages show the same controls, each with its
  own Q-Link bank — intended.
- **Popup picker fields**: two independent 1.0.1 bugs fixed. (1) Z-order — the field key is
  now placed AFTER the value label in `componentsData` (later = on top) and its art became a
  transparent window (`key_pics` `field=True`: border + chevron only, lit = accent wash at
  alpha 150, RGBA saved without the RGB convert), so the whole field rect is the Button's
  touch target; the label shows through the window. (2) `FIELD_ACTIONS` dropped
  "Enter Pressed" — a press+enter pair toggled the popup twice (opened then instantly
  closed). One Mouse Down toggle per tap; tap-again-to-close kept (user's choice).

## 1.0.3 (2026-10-23)

- **Delay feedback split** (`delay_l_fb`/`delay_r_fb`): the vendored Doublelay port's single feedback became two
  independent loops; at equal knob values the reduction is the 1.0.2 operation (the `verb-delay` golden and the
  equal-knob test pin it). `delay_fb` is retired in place (key/param kept, read by nothing, control gone) and leaves
  every `DEST_KEYS`/Q-Link bank.
- **FX RESET** (`fx_reset`, momentary): restores the selected voice's 19 FX parameters to their defaults and bumps
  `rev` so the wrapper reports each change through its display_rev diff; the momentary is a PK_TRIG and never enters
  the state chunk.
- **Gated verb LENGTH** (BA3 tap count): both 490-entry prime tables vendored verbatim; reachable offsets are 0..487
  (the vendor clamp `start = 488 - length`), rings sized `prime[487]+1`; reads happen before the write/advance
  (sample-equivalent to the vendor's count+prime read, and the smallest prime is 3, so the current sample is never
  read — the design's "all primes ≥ 6247" was stale, corrected there); the divisor is `cbrt(length)` cached per knob
  position (exact 1.0 at length 1, which is bit-identical to 1.0.2).
- **Length cap 128 taps** (design §4 escape hatch, measured): the worst-case bench (`-w`: 12 voices, every FX, both
  delay feedbacks) at the vendor's full 488 taps measured p99 70.8 % / worst 133.6 % of a block on x86 vs the 1.0.2
  baseline's 16.2 / 59.0; 256 taps still measured 40.1 / 114.9, so the mapping is capped at
  `(int)(pow(knob/127,2)*127)+1` = 1..128 taps (26.6 / 93.0 — within the ≤ ~2× baseline p99 and < 100 % worst
  release criterion). Readout: 0→1, 64→33, 127→128.
- **Three FX mixes** (`ring_mix`/`fm_mix`/`comb_mix`): engagement 0 bypasses regardless of mix; 127 is the pure wet
  (bit-identical to 1.0.2, no extra arithmetic via `knob01()`'s exact 1.0); 0 is exact dry; between, an equal-gain
  linear blend. The comb's engagement gate skips `fx_comb` entirely at 0; inside, mix 0 returns x without touching
  the buffer.
- **Picker fields rebuilt on the framework's popup widget**: one `sdField_<tab>_<key>` def per picker — two
  "Toggle Switch" actions, components exactly [Focus, Value label on its own "Text" handle], no Button (a Button
  fires the actions twice: open, instantly shut); the placement maps Data→the hidden `<key>__open` flag and
  Text→the enum. The border + chevron bake into the page background; `tools/field_check.py` gates the built TUI.json.
- **Violet retheme**: accent `5a2494`, dim `341948`, lit edge `a67dd2`, tab ink `f4eff9`, LED off `322242`; the
  layout's three `theme_*` literals and the six page previews were reviewed; the warm-color sweep leaves only the
  neutral greys.
- **Vendored tool patches** (recorded here per the `shadow_skin.py` precedent): `tools/host_test.c` gained the 1.0.3
  `fx_reset` scenario (19 default readbacks, host automation reports, absent-from-chunk); `tools/bench.c` gained
  `-w` (worst-case preset by parameter name, so the same binary benches the 1.0.2 baseline; unknown names skipped).
  The wrapper (`vst2_wrap.c`) is NOT patched: this branch links the owner's local copy in `src/wrapper/` (the
  one-click-one-unit wheel fix from `main`), and `third_party/` stays byte-identical.
- **Goldens**: `test/goldens/fx-sound.txt` was captured at `972c519` (pristine 1.0.2) before any 1.0.3 change; the
  `verb-delay` scenario uses the legacy `delay_fb` knob, which is exactly the equal-knob reduction the split must
  reproduce, so it pins both the fb math and the verb's length-1 bit identity. All three hashes stayed green through
  every task.

## 1.1.0 (2026-10-25)

- **Mixer params are aliases, not copies** (design §3): 24 appended `PK_MIX` params (`mix_vol_1..12`,
  `mix_pan_1..12`, VST indices 97..120) route in `set_param`/`get_param` straight to `s->v[k].p[P2E[P_OUT_VOL|
  P_OUT_PAN]]` — one source of truth, so KIT INIT / COPY / PASTE / the chunk (the twelve voice lines already
  carry the values) and the old `out_pan`/`out_vol` indices all agree with the faders for free; no new
  serialization. A mixer write to the *selected* voice bumps `rev++` so the wrapper's display_rev diff refreshes
  the old VOL/PAN readbacks for the host. `EDIT_PARAMS` stays 86 and `N_LFO1_DEST` 65 (guard CHECKs) — `PK_MIX`
  never enters `p[]` or the LFO DEST list, and no released param was renamed (the relabels are panel text only:
  `params.json`/DEST byte-identical, so panel words and host names now deliberately diverge).
- **Master EQ**: nine appended `PK_MASTER` params (121..129) live device-level in `Synth.meq[]` with per-channel
  `PeakEQ` states (`meq_hs[2]`, `meq_pk[2]`, `meq_ls[2]`); the chunk gains one `meq=%g,...` line after `base=`
  (old parsers skip it, a missing line lands on defaults — chunk 2596 → 2624 B). `patch_init_kit()` owns the
  defaults (64/0/64 per band) **and the state zeroing**, because KIT INIT resets the master EQ (the author's
  call; `create()` and `deserialize()` run the same function, so absent-line = flat falls out). The `_display`
  gate now accepts `PK_MASTER` and reads `s->meq[j - P_MEQ_HI_FREQ]` — never `P2E[j]` (it is -1 for these).
  Chain position in `render()`: voices → `MASTER_GAIN` → the three bands → NaN guard → soft clip → int16; band
  order high shelf → peak → low shelf, coefficients once per block (mirroring `voice.c`'s per-block `eq_set`,
  same `40*300^(v/127)` / `±18 dB` / `Q 0.5..6` expressions — kept in sync), per-channel states. **Bypass rule**:
  each band is skipped entirely when its raw gain param is 0 (never stepped with a "neutral" coefficient), so a
  flat master EQ is bit-identical to 1.0.3 — the goldens (fixture at `972c519`) prove it every suite run. The
  shelves are RBJ cookbook high/low shelving equations reusing `PeakEQ` (`eq_set_high_shelf`/`eq_set_low_shelf`
  beside `eq_set`); probe-checked: +12 dB at a 700 Hz corner measures ×3.998 (10 kHz) / ×3.887 (50 Hz), the
  other band edge ×0.99–0.99.
- **Panel relabels** (design §1): every prefix stripped per the approved table (DELAY DETUNE → DETUNE, TIME L/R,
  FEEDBACK L/R, DELAY WET → MIX, L/R SYNC → SYNC L/R, RESET → FX RESET, SRC OSC/NOISE → OSC/NOISE, the OSC /
  NOISE / P.ENV / F.ENV / N.FILTER prefixes, KEY TRACK → TRACK, VEL→CUT / N.FILTER VEL → Velocity) — strings
  in `tools/skin.py` only; the host param names did not move (author's ruling: nothing under the hood for labels).
- **Gated Verb knob swap**: `gverb_length` takes the left slot, `gverb` ("MIX") the right; the FX Q-Link banks
  swap to match (bank 1 ends `gverb_length`, bank 2 starts `gverb`). CTRL's bank shrinks to 6 keys with the
  OUTPUT panel gone (`out_sel` is now control-less — it was state-only in the engine, so nothing audible
  changes); KIT (16,269,371,343) and CHOKE (399,269,595,343) widen into the hole, ABOUT unchanged.
- **MASTER tab** (design §5): seventh page between LFO and CTRL — MIXER (16,48,856,564: two rows of six
  columns, a 36 px PAN knob + 44×20/13 px readout above each labelled fader, travel ~113/111 px, caps stay the
  standard 44 px) and MASTER EQ (888,48,376,564: three Filter-tab-shaped rows HIGH/EQ/LOW × FREQ/GAIN/Q at
  y 170/341/512). Geometry pinned by the approved mockup `mockups/master-tab.jpg` (generator
  `mockups/draw_master_tab.py`, throwaway — not part of the build). `value()` gained a `px` argument for the
  13 px readouts. Q-Link banks: five (8+8+8+8+1), mixer column-pairs then the nine EQ knobs; `layout.conf`
  `[tab MASTER]` carries the 33 keys, and `build()`'s assert gates that every one has a control on the page.
- **Browser tile**: `art/tile.png` regenerated by `tools/tile.py` (which reads the live skin) — the pre-retheme
  orange (`#f28c1c`) is gone (2712 px of `#5a2494` in its place); the build copies it to
  `Plugin Skins/browser_images/soundsmode.png`.
- **No vendored patches this release**: `host_test.c` and `bench.c` are untouched (the host test is
  `NPARAMS`-driven — 139 params flow through it — and the `-w` preset matches params by name, which did not
  change); `third_party/` stays byte-identical.
- **Bench vs 6ba97b4** (x86, relative): defaults 5.8 / 33.7 (baseline 4.9 / 33.8 — the flat EQ is free);
  `-w` p99 28.4–29.7 vs baseline 30.0 (≤ ~2× criterion met), worst block 91.1–100.9 vs baseline 96.5 (single-
  block jitter; the EQ's gains are not in the preset). Binaries: armhf 80 800 B, aarch64 104 600 B, max GLIBC
  2.27, one exported `T` each. The aarch64 path needed binfmt re-registered
  (`docker run --privileged --rm tonistiigi/binfmt --install arm64`) — it does not survive restarts.
