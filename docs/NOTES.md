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
