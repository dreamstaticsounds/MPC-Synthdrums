#!/usr/bin/env python3
"""The single source of MPC Synthdrums' parameters: writes params.json (mpc-vst-plugins) and src/params_gen.h.

    python3 tools/gen_params.py          (writes both)
    python3 tools/gen_params.py --check  (fails when either is stale)

MPC saves a project's values by parameter index: once released, parameters are only ever appended to this list.

Values are in the units the original's panel gives (0..127, -63..+63, -24..+24 ...); the wrapper sends whole
numbers for "display": "int" parameters and the option's index (0-based: VOICE 0 is the first voice, 11 the
twelfth) for option lists, and the engine keeps them as they are (src/synth.h: a patch is one float per
parameter per voice).

The edit panel edits the selected voice (src/patches.c): the PK_PATCH params in VST index order (P_PITCH ..
P_CHOKE first, EDIT_PARAMS of them). New PK_PATCH params may append anywhere after index 34; P2E/E2P in
params_gen.h map VST enum order <-> p[] edit order.
`pitch` comes first: the framework's host test turns a data wheel click into 1% of a param's range and
expects one step, so the first "display": "int" param must span at most 100 (-24..24 = 48 here; a
0..127 param would read 1.27 and round to two steps -- GAIAXXL's first int is pitch for the same reason).

Kinds (params_gen.h):
  PK_PATCH  stored in a patch (one voice's sound) -- EDIT_PARAMS of them, one edit panel
  PK_UI     the voice selector, the keyboard base note: in the plugin state, not in a patch
  PK_TRIG   a key (momentary): the voice arrows, the audition trigger, KIT INIT
  PK_SHOW   read-only text (the selected voice's name)"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FILTER_MODES = ["LP", "BP", "HP"]
OUT_SELS = ["1", "2", "3"]
CHOKE_NAMES = ["OFF", "G1", "G2", "G3", "G4"]
VOICE_NAMES = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"]


def P(key, name, lo=0, hi=127, d=0, opts=None, kind="PK_PATCH", **extra):
    p = {"key": key, "name": name, "kind": kind, "default": d}
    if opts:
        p["options"] = opts
        p["min"], p["max"] = 0, len(opts) - 1
    else:
        p["min"], p["max"] = lo, hi
        p["display"] = "int"
    p.update(extra)
    return p


PARAMS = [
    # PITCH (first: the host test's wheel-step check wants a narrow first int param -- see the docstring)
    P("pitch", "PITCH", -24, 24, 0),
    # CONTROL (the five velocity routings)
    P("vel_to_vol", "VEL\u2192VOL", 0, 127, 127),
    P("vel_to_cut", "VEL\u2192CUT", 0, 127, 0),
    P("vel_to_noise", "VEL\u2192NOISE", 0, 127, 0),
    P("vel_to_osc", "VEL\u2192OSC", 0, 127, 0),
    P("vel_to_penv", "VEL\u2192P.ENV", 0, 127, 0),
    # SOURCE
    P("src_mix", "SRC OSC/NOISE", 0, 127, 0),
    # OSCILLATOR
    P("osc_decay", "OSC DECAY", 0, 127, 64),
    P("osc_shape", "OSC SHAPE", 0, 127, 0),
    P("osc_xmod", "OSC X-MOD", 0, 127, 0),
    # PITCH
    P("pitch_track", "KEY TRACK", 0, 127, 0),
    P("penv_attack", "P.ENV ATTACK", 0, 127, 0),
    P("penv_decay", "P.ENV DECAY", 0, 127, 0),
    P("penv_shape", "P.ENV SHAPE", 0, 127, 64),
    P("penv_amount", "P.ENV AMOUNT", -63, 63, 0),
    # NOISE
    P("noise_colour", "NOISE COLOUR", 0, 127, 64),
    P("noise_attack", "NOISE ATTACK", 0, 127, 0),
    P("noise_decay", "NOISE DECAY", 0, 127, 64),
    P("noise_shape", "NOISE SHAPE", 0, 127, 64),
    P("noise_filter", "NOISE CUTOFF", 0, 127, 127, dynamic_display=True),
    # STUTTER
    P("stutter_length", "STUTTER RATE", 0, 127, 64, dynamic_display=True),
    P("stutter_strength", "STUTTER STR", 0, 127, 0),
    # FILTER
    P("filter_mode", "FILTER MODE", d=0, opts=FILTER_MODES),
    P("filter_res", "RES", 0, 127, 0),
    P("filter_cutoff", "CUTOFF", 0, 127, 127, dynamic_display=True),
    P("fenv_amount", "F.ENV AMOUNT", -63, 63, 0),
    P("fenv_decay", "F.ENV DECAY", 0, 127, 64),
    # DIST
    P("dist_drive", "DIST DRIVE", 0, 127, 0),
    # EQ
    P("eq_freq", "EQ FREQ", 0, 127, 64, dynamic_display=True),
    P("eq_gain", "EQ GAIN", -63, 63, 0),
    P("eq_q", "EQ Q", 0, 127, 64),
    # OUTPUT
    P("out_pan", "PAN", -64, 63, 0),
    P("out_vol", "VOL", 0, 127, 100),
    P("out_sel", "OUT SEL", d=0, opts=OUT_SELS),
    P("choke", "CHOKE", d=0, opts=CHOKE_NAMES),
    # the voice selector, the keyboard assignment
    P("voice", "VOICE", d=0, opts=VOICE_NAMES, kind="PK_UI"),
    P("base_note", "BASE NOTE", 0, 127, 36, kind="PK_UI"),
    # the keys
    P("trig", "TRIG", kind="PK_TRIG", momentary=True),
    P("voice_prev", "VOICE <", kind="PK_TRIG", momentary=True, step_of="voice", step_delta=-1),
    P("voice_next", "VOICE >", kind="PK_TRIG", momentary=True, step_of="voice", step_delta=1),
    P("kit_init", "KIT INIT", kind="PK_TRIG", momentary=True),
    # the readout
    P("voice_name", "VOICE NAME", 0, 0, 0, kind="PK_SHOW", display="string"),
    # the voice clipboard (Task 3): snapshots/pastes/resets the selected voice
    P("voice_copy", "COPY VOICE", kind="PK_TRIG", momentary=True),
    P("voice_paste", "PASTE VOICE", kind="PK_TRIG", momentary=True),
    P("voice_init", "INIT VOICE", kind="PK_TRIG", momentary=True),
    # the noise branch's filter (Task 4): a proper biquad with its own envelope, appended (append-only rule)
    P("nfilt_res", "N.FILTER RES", 0, 127, 0),
    P("nfilt_mode", "N.FILTER MODE", d=0, opts=FILTER_MODES),
    P("nfilt_env_amount", "N.FENV AMOUNT", -63, 63, 0),
    P("nfilt_env_decay", "N.FENV DECAY", 0, 127, 64),
    P("nfilt_vel", "N.FILTER VEL", 0, 127, 0),

    # EQ bands 2 and 3 (band 1 keeps eq_freq/eq_gain/eq_q): the same 40 Hz..12 kHz log sweep,
    # ±18 dB and Q 0.5..6 mapping as band 1, appended so 1.0.0 patches load unchanged.
    P("eq2_freq", "EQ2 FREQ", 0, 127, 64, dynamic_display=True),
    P("eq2_gain", "EQ2 GAIN", -63, 63, 0),
    P("eq2_q", "EQ2 Q", 0, 127, 64),
    P("eq3_freq", "EQ3 FREQ", 0, 127, 64, dynamic_display=True),
    P("eq3_gain", "EQ3 GAIN", -63, 63, 0),
    P("eq3_q", "EQ3 Q", 0, 127, 64),

    # The FX row (spec §8): ring mod, operator FM, comb, gated reverb. Each is off (exact bypass) at 0.
    P("ring_mod", "RING MOD", 0, 127, 0, dynamic_display=True),
    P("fm_index", "FM INDEX", 0, 127, 0),
    P("comb", "COMB", 0, 127, 0, dynamic_display=True),
    P("gverb", "GATED VERB", 0, 127, 0),

    # The stereo delay (spec §8): Airwindows Doublelay, vendored, last in the chain. Wet 0 = exact bypass.
    P("delay_detune", "DELAY DETUNE", 0, 127, 64, dynamic_display=True),
    P("delay_l", "DELAY L", 0, 127, 64, dynamic_display=True),
    P("delay_r", "DELAY R", 0, 127, 64, dynamic_display=True),
    P("delay_fb", "DELAY FB", 0, 127, 64),   # 0..127, effective 0..0.95 -- RETIRED 1.0.3: read by
    # nothing (kept for append-only compat; the sound moved to delay_l_fb / delay_r_fb)
    P("delay_wet", "DELAY WET", 0, 127, 0),
]

# The three LFOs (spec §6): per-voice, bidirectional depth, free-run or tempo-synced, retriggered with
# the hit by default (drum-natural). Append order: lfo1_* then lfo2_* then lfo3_* (the append-only rule);
# the destination's option list is the DEST list, patched in below once the whole list is known.
_lfo = []
for _n in (1, 2, 3):
    _lfo += [
        P("lfo%d_rate" % _n, "LFO%d RATE" % _n, 0, 127, 64, dynamic_display=True),
        P("lfo%d_sync" % _n, "LFO%d SYNC" % _n, d=0, opts=["OFF", "ON"]),
        P("lfo%d_div" % _n, "LFO%d DIV" % _n, d=6,
          opts=["1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "1/8D", "1/4D",
                "1/2D"]),
        P("lfo%d_retrig" % _n, "LFO%d RETRIG" % _n, d=1, opts=["OFF", "ON"]),
        P("lfo%d_wave" % _n, "LFO%d WAVE" % _n, d=0,
          opts=["sin", "tri", "saw up", "saw dwn", "sqr", "s&h", "rnd"]),
        P("lfo%d_dest" % _n, "LFO%d DEST" % _n, d=0, opts=["x"]),   # the DEST list is patched in below
        P("lfo%d_depth" % _n, "LFO%d DEPTH" % _n, -63, 63, 0),
    ]
PARAMS += _lfo

# 1.0.2 (design section 8): appended, append-only rule. dist_tone and width are continuous so they
# join the LFO dest list at its end (existing dest indices stay stable); the sync switches have
# options and are excluded automatically.
PARAMS += [
    P("dist_tone", "DIST TONE", 0, 127, 127),
    P("width", "WIDTH", 0, 127, 0),
    P("delay_l_sync", "DELAY L SYNC", d=0, opts=["OFF", "ON"]),
    P("delay_r_sync", "DELAY R SYNC", d=0, opts=["OFF", "ON"]),
]

# 1.0.3 (design sections 2, 3, 4, 6): appended, append-only rule. fx_reset is a PK_TRIG momentary
# exactly like kit_init (the wrapper springs it back; the engine keeps it out of p[] and the chunk).
# The delay fb knobs default 64 (= the retired delay_fb 64); the verb length defaults 0 (= the
# 1.0.2 baked one-tap verb) and reads as its tap count (dynamic_display, engine branch in Task 5);
# the three mixes default 127 = pure wet = the 1.0.2 engaged sound. The six continuous ones join
# the LFO dest list at its end (existing dest indices stay stable).
PARAMS += [
    P("delay_l_fb", "DELAY L FB", 0, 127, 64),
    P("delay_r_fb", "DELAY R FB", 0, 127, 64),
    P("fx_reset", "FX RESET", kind="PK_TRIG", momentary=True),
    P("gverb_length", "GVERB LENGTH", 0, 127, 0, dynamic_display=True),
    P("ring_mix", "RING MIX", 0, 127, 127),
    P("fm_mix", "FM MIX", 0, 127, 127),
    P("comb_mix", "COMB MIX", 0, 127, 127),
]

# 1.1.0 (design §3): appended, append-only rule. The mixer's faders/pans are PK_MIX aliases, not
# copies: the engine routes mix_vol_k/mix_pan_k straight into voice k's own p[P_OUT_VOL]/p[P_OUT_PAN]
# (no separate state to serialize -- the twelve voice lines already carry them), so they are excluded
# from p[]/EDIT_PARAMS and from the LFO DEST list (DESTs are PK_PATCH only) by kind.
PARAMS += [
    *[P("mix_vol_%d" % i, "VOL %d" % i, 0, 127, 100, kind="PK_MIX") for i in range(1, 13)],
    *[P("mix_pan_%d" % i, "PAN %d" % i, -64, 63, 0, kind="PK_MIX") for i in range(1, 13)],
]

# The master EQ (1.1.0 design §3/§4): nine device-level knobs (PK_MASTER -- not per-voice, not in
# p[], not LFO destinations), three bands of FREQ/GAIN/Q mirroring the voice EQ's mappings (40 Hz..
# 12 kHz log, +-18 dB, Q 0.5..6). Stored in Synth.meq[], one appended `meq=` line in the chunk;
# KIT INIT returns them to flat (author's decision, design §3). Contiguous offsets from
# P_MEQ_HI_FREQ: freqs +0/+3/+6, gains +1/+4/+7, qs +2/+5/+8.
PARAMS += [
    P("meq_hi_freq", "MASTER HIGH FREQ", 0, 127, 64, dynamic_display=True, kind="PK_MASTER"),
    P("meq_hi_gain", "MASTER HIGH GAIN", -63, 63, 0, kind="PK_MASTER"),
    P("meq_hi_q",    "MASTER HIGH Q",    0, 127, 64, kind="PK_MASTER"),
    P("meq_freq",    "MASTER EQ FREQ",   0, 127, 64, dynamic_display=True, kind="PK_MASTER"),
    P("meq_gain",    "MASTER EQ GAIN",   -63, 63, 0, kind="PK_MASTER"),
    P("meq_q",       "MASTER EQ Q",      0, 127, 64, kind="PK_MASTER"),
    P("meq_lo_freq", "MASTER LOW FREQ",  0, 127, 64, dynamic_display=True, kind="PK_MASTER"),
    P("meq_lo_gain", "MASTER LOW GAIN",  -63, 63, 0, kind="PK_MASTER"),
    P("meq_lo_q",    "MASTER LOW Q",     0, 127, 64, kind="PK_MASTER"),
]

# The LFO destinations (DEST): every continuous voice parameter (PK_PATCH without options -- the enums
# are excluded per spec), in panel order. The labels are what the MPC shows in the dest option list;
# params_gen.h emits DEST_KEYS (the engine's lookup) and DEST2P (the VST enum index per dest).
_dests = [p for p in PARAMS if p["kind"] == "PK_PATCH" and not p.get("options")]
assert len({p["name"] for p in _dests}) == len(_dests), "DEST labels must be unique"
DEST_LABELS = [p["name"] for p in _dests]
DEST_KEYS = [p["key"] for p in _dests]
for p in PARAMS:
    if p["key"].startswith("lfo") and p["key"].endswith("_dest"):
        p["options"] = list(DEST_LABELS)
        p["min"], p["max"] = 0, len(DEST_LABELS) - 1

for p in PARAMS:   # momentary keys: a plain 0..1 with no int display
    if p.get("momentary"):
        p["min"], p["max"] = 0, 1
        p.pop("display", None)

# the edit panel: PK_PATCH params in VST index order define p[] edit order (append-only). The 1.0.0 block
# must keep VST indices 0..34 (old MPC projects save by index); later appends may sit after the UI/trig params.
_edit = [i for i, p in enumerate(PARAMS) if p["kind"] == "PK_PATCH"]
assert _edit[:35] == list(range(35)), "the 1.0.0 edit block must keep VST indices 0..34"
assert (PARAMS[_edit[0]]["key"], PARAMS[_edit[34]]["key"]) == ("pitch", "choke"), "P_PITCH .. P_CHOKE"
EDIT_KEYS = [PARAMS[i]["key"] for i in _edit]          # p[] order (append-only from here on)
EDIT_PARAMS = len(_edit)
_edit_def = {PARAMS[i]["key"]: PARAMS[i]["default"] for i in _edit}


def params_json():
    out = []
    for p in PARAMS:
        q = {k: v for k, v in p.items() if k != "kind"}
        out.append(q)
    return json.dumps({"name": "MPC Synthdrums", "params": out}, indent=1) + "\n"


def cname(key):
    return "P_" + key.upper()


def header():
    L = ["/* GENERATED by tools/gen_params.py: do not edit. MPC Synthdrums' parameters, in VST index order. */",
         "#pragma once", "", "typedef enum { PK_PATCH, PK_UI, PK_TRIG, PK_SHOW, PK_MIX, PK_MASTER } ParamKind;", "",
         "typedef struct { const char *key; ParamKind kind; float min, max, def; int nopts; } ParamDef;", "",
         "enum {"]
    for p in PARAMS:
        L.append("    %s," % cname(p["key"]))
    L += ["    P_COUNT", "};", "",
          "/* the edit panel's first parameter: the contiguous PK_PATCH block's anchor (p[] index 0) */",
          "#define P_EDIT_FIRST %s" % cname(PARAMS[_edit[0]]["key"]), "",
          "/* the edit panel: one contiguous PK_PATCH block (the selected voice), P_PITCH .. P_CHOKE */",
          "#define EDIT_PARAMS %d" % EDIT_PARAMS, "",
          "/* enum -> p[] edit index (-1 when the param is not PK_PATCH), and its inverse */",
          "static const short P2E[P_COUNT] = {"]
    _p2e = {i: k for k, i in enumerate(_edit)}
    for i, p in enumerate(PARAMS):
        L.append("    %s," % _p2e.get(i, -1))
    L += ["};", "", "static const short E2P[EDIT_PARAMS] = {"]
    for i in _edit:
        L.append("    %d," % i)
    L += ["};", "",
          # symbolic names, not the index values: a compiler-checked assert. (Emitting the literal
          # indices would make it tautological -- 0 == 0 && 34 == 34 -- able to catch nothing.)
          "_Static_assert(%s == 0 && %s == 34, \"P_PITCH..P_CHOKE must keep VST indices 0..34\");"
          % (cname(PARAMS[_edit[0]]["key"]), cname(PARAMS[_edit[34]]["key"])), "",
          "/* the init patch: PDEF's defaults in p[] edit order (patch_init_voice copies it) */",
          "static const float INIT_PATCH[EDIT_PARAMS] = {%s};" % ", ".join("%g" % _edit_def[k] for k in EDIT_KEYS),
          "",
          "static const ParamDef PDEF[P_COUNT] = {"]
    for p in PARAMS:
        L.append('    {"%s", %s, %d, %d, %g, %d},' % (p["key"], p["kind"], p["min"], p["max"], p["default"],
                                                      len(p.get("options") or [])))
    L += ["};", ""]
    # The option params: their count and their name array, generically. Existing C code keeps its
    # names via the override dict; new params get LFO1_WAVE, NFILT_MODE, ... (spec: one emitter).
    _override = {"choke": "CHOKE_NAMES", "voice": "VOICE_NAMES"}
    _opts = [p for p in PARAMS if p.get("options")]
    for p in _opts:
        L.append("#define N_%s %d" % (p["key"].upper(), len(p["options"])))
    L.append("")
    # The LFO destinations (spec §6): every continuous voice parameter. The label list is the
    # lfoN_dest option list; the keys and the VST enum indices are the engine's block-rate lookup.
    _idx = {p["key"]: i for i, p in enumerate(PARAMS)}
    L += ["/* the LFO destinations: every continuous voice parameter (PK_PATCH without options) */",
          "static const char *const DEST_KEYS[N_LFO1_DEST] = {%s};" % ", ".join('"%s"' % k for k in DEST_KEYS),
          "static const short DEST2P[N_LFO1_DEST] = {%s};" % ", ".join(str(_idx[k]) for k in DEST_KEYS),
          ""]
    for p in _opts:
        L.append("static const char *const %s[N_%s] = {%s};"
                 % (_override.get(p["key"], p["key"].upper() + "S"), p["key"].upper(),
                    ", ".join('"%s"' % s for s in p["options"])))
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    targets = {os.path.join(ROOT, "params.json"): params_json(), os.path.join(ROOT, "src", "params_gen.h"): header()}
    if "--check" in sys.argv:
        for f, t in targets.items():
            if not os.path.exists(f) or open(f).read() != t:
                raise SystemExit("%s is stale: run python3 tools/gen_params.py" % f)
    else:
        for f, t in targets.items():
            open(f, "w").write(t)
        print("%d parameters" % len(PARAMS))
