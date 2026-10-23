#!/usr/bin/env python3
"""MPC Synthdrums' six pages: their geometry, their artwork and every control in MPC's skin (TUI.json).

    python3 tools/skin.py --layout              writes layout.conf (the tabs and their Q-Links, for mpc-vst-plugins)
    python3 tools/skin.py "<skin>/Plugin Skins" builds the pages into the skin gen_vst.py made from layout.conf
    python3 tools/skin.py --preview DIR         draws each page with its controls at their defaults (offline check)

Pages (1280 x 628, MPC's whole plugin area): OSC, PITCH, FILTER, FX (the four voice pages), LFO (the three
per-voice modulators) and CTRL (the global page). Each page shows only its right-aligned name: hitting a pad
selects that voice for editing (and plays it), so the old VOICE selector strip (segments, name, TRIG) is gone
and its space goes to bigger controls. The panels carry the original DSK SynthDrums' sections in the
original's order, with the original's short labels.

Look: the original's black-and-orange panel: near-black ground, dark grey panels, orange frames and tabs, warm
white lettering, dark value windows. The caps: tools/caps.py (aluminium-top knobs). The lettering: DejaVu Sans
Condensed Bold (art/fonts): 18 px labels, 24 px value windows. No curve displays (1.0.1): the panels are static
art drawn once into the page's background (ImageDraw); the knobs, keys, option rows and value windows are live
controls over it."""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import caps  # noqa: E402
import gen_params  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "art", "fonts", "DejaVuSansCondensed-Bold.ttf")
W, H = 1280, 628
Y0 = 86            # Force Shadow's y of the plugin area's top (layout.conf coordinates)
SS = 3             # supersampling for drawn shapes

# ---------------------------------------------------------------- colours (the original's black and orange)
BG = (18, 18, 18)          # theme_bg 121212
PANEL = (32, 32, 32)
PANEL_DIM = (25, 25, 25)
ART_BG = (13, 13, 13)
ACCENT = (242, 140, 30)    # theme_accent f28c1e
ACCENT_DIM = (118, 66, 15)
INK = (240, 238, 234)      # theme_ink f0eeea
INK_DIM = (190, 186, 182)  # theme_ink_dim bebab6
TAB_INK = (26, 18, 6)      # the ink on an orange tab
LCD = (22, 21, 20)
LCD_EDGE = (74, 70, 66)
LCD_INK = "f0eeea"
LED_OFF = (66, 46, 24)
LED_ON = ACCENT
KEY_FACE = (54, 52, 51)
SLOT = (12, 12, 12)
TICK = (122, 118, 114)
GRID = (46, 44, 42)

KNOB_FRAMES = 128
KNOB_SPAN = 150
MAX_STRIP = 12288

TABS = ["OSC", "PITCH", "FILTER", "FX", "LFO", "CTRL"]
PARAMS = gen_params.PARAMS
INDEX = {p["key"]: i for i, p in enumerate(PARAMS)}
PBYKEY = {p["key"]: p for p in PARAMS}

_fonts = {}


def font(px):
    if px not in _fonts:
        _fonts[px] = ImageFont.truetype(FONT, px)
    return _fonts[px]


def R(v):
    return int(round(v))


# ---------------------------------------------------------------- the page model
class Page:
    """a page: its background drawing and its live controls"""

    def __init__(self, name):
        self.name = name
        self.bg = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.bg)
        self.controls = []     # dicts: kind, key, rect, extra

    def add(self, kind, key, rect, **kw):
        assert key in INDEX, key
        c = dict(kind=kind, key=key, rect=tuple(R(v) for v in rect))
        c.update(kw)
        self.controls.append(c)
        return c


# ---------------------------------------------------------------- drawing helpers (on the background)
def rrect(pg, rect, fill=None, outline=None, width=1, radius=8):
    x, y, w, h = rect
    big = Image.new("RGBA", (R(w * SS), R(h * SS)), (0, 0, 0, 0))
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=radius * SS,
                                          fill=fill + (255,) if fill else None,
                                          outline=outline + (255,) if outline else None, width=R(width * SS))
    sm = big.resize((R(w), R(h)), Image.LANCZOS)
    base = pg.bg.convert("RGBA")
    base.alpha_composite(sm, (R(x), R(y)))
    pg.bg = base.convert("RGB")
    pg.d = ImageDraw.Draw(pg.bg)


def text(pg, xy, s, px=16, fill=INK, anchor="mm"):
    pg.d.text(xy, s, font=font(px), fill=fill, anchor=anchor)


def fit_text(pg, rect, s, px=16, fill=INK):
    """centred in rect, condensed sideways if the word is wider (never smaller than px)"""
    x, y, w, h = rect
    f = font(px)
    tw = f.getlength(s)
    if tw <= w - 4:
        text(pg, (x + w / 2, y + h / 2), s, px, fill)
        return
    big = Image.new("RGBA", (R(tw) + 8, px * 2), (0, 0, 0, 0))
    ImageDraw.Draw(big).text((4, px), s, font=f, fill=fill + (255,), anchor="lm")
    big = big.resize((R(w - 4), px * 2), Image.LANCZOS)
    base = pg.bg.convert("RGBA")
    base.alpha_composite(big, (R(x + 2), R(y + h / 2 - px)))
    pg.bg = base.convert("RGB")
    pg.d = ImageDraw.Draw(pg.bg)


def panel(pg, rect, title, accent=ACCENT, dim=False):
    """a section: dark grey, a frame in the accent and a title tab"""
    x, y, w, h = rect
    rrect(pg, rect, fill=PANEL_DIM if dim else PANEL, outline=accent, width=2, radius=10)
    tw = font(20).getlength(title) + 26
    rrect(pg, (x + 8, y + 6, tw, 28), fill=accent, radius=7)
    text(pg, (x + 8 + tw / 2, y + 20), title, 20, TAB_INK)


def knob_scale(pg, cx, cy, r, bipolar=False):
    """the ticks around a parameter knob"""
    for k in range(11):
        a = math.radians(-KNOB_SPAN + 2 * KNOB_SPAN * k / 10 - 90)
        r0, r1 = r + 3, r + (9 if k in (0, 10) or (bipolar and k == 5) else 6)
        pg.d.line((cx + r0 * math.cos(a), cy + r0 * math.sin(a), cx + r1 * math.cos(a), cy + r1 * math.sin(a)),
                  fill=TICK, width=2)


def slot(pg, cx, y0, y1, bipolar=False):
    """a fader's slot and its ticks"""
    rrect(pg, (cx - 6, y0, 12, y1 - y0), fill=SLOT, radius=5)
    for k in range(11):
        y = y0 + 8 + (y1 - y0 - 16) * k / 10
        ln = 12 if k in (0, 10) or (bipolar and k == 5) else 7
        pg.d.line((cx - 10 - ln, y, cx - 10, y), fill=TICK, width=2)
        pg.d.line((cx + 10, y, cx + 10 + ln, y), fill=TICK, width=2)


# ---------------------------------------------------------------- live controls (registered, drawn by build())
def knob(pg, key, cx, cy, size, label=None, endless=False, lcd_rect=None, label_px=16, label_fill=INK, bipolar=None,
         label_w=96):
    if bipolar is None:
        p = PBYKEY[key]
        bipolar = p.get("min", 0) < 0
    if label:
        fit_text(pg, (cx - label_w / 2, cy - size / 2 - 27, label_w, 22), label, label_px, label_fill)
    if not endless:
        knob_scale(pg, cx, cy, size / 2, bipolar)
    pg.add("knob", key, (cx - size / 2, cy - size / 2, size, size), endless=endless)
    if lcd_rect is None:
        lcd_rect = (cx - 42, cy + size / 2 + 8, 84, 30)
    if lcd_rect:
        value(pg, key, lcd_rect)


def value(pg, key, rect):
    lcd(pg, rect)
    pg.add("value", key, rect)


CAP_COLOURS = {"white": ((236, 234, 228), (25, 25, 25)), "grey": ((172, 172, 170), (25, 25, 25)),
               "black": ((34, 33, 33), (245, 245, 245))}


def lcd(pg, rect):
    rrect(pg, rect, fill=LCD, outline=LCD_EDGE, width=1.5, radius=4)


def fader(pg, key, cx, top, bottom, label, cap="white", label_fill=INK, cap_w=44):
    """label, value window, then the fader (its slot drawn here, the cap a filmstrip)"""
    p = PBYKEY[key]
    fit_text(pg, (cx - 32, top, 64, 20), label, 16, label_fill)
    value(pg, key, (cx - 28, top + 22, 56, 26))
    y0 = top + 54
    slot(pg, cx, y0, bottom, p.get("min", 0) < 0)
    pg.add("slider", key, (cx - 30, y0 - 2, 60, bottom - y0 + 4), cap=cap, cap_w=cap_w)


def radio(pg, key, rects, words, style="led", px=16, colour=ACCENT):
    """an option list: one picture pair per option; style led (a LED and the word) or key (a key lit orange)"""
    for i, (r, w) in enumerate(zip(rects, words)):
        pg.add("opt", key, r, index=i, count=len(rects), word=w, style=style, px=px, colour=colour)


def toggle(pg, key, rect, word, px=16, colour=ACCENT):
    pg.add("key", key, rect, word=word, px=px, colour=colour)


def momentary(pg, key, rect, word, px=16, arrow=None):
    pg.add("key", key, rect, word=word, px=px, colour=ACCENT, arrow=arrow)


def popup(pg, key, rect, cols):
    """a field that opens the option list on tap (mpc-vst-plugins' popup control, docs/NOTES.md): build_page
    makes the field a key on the hidden "<key>__open" param plus the enum's own value label, and the list
    itself option keys shown only while that flag is on (IndexedEnabling), on top of the page"""
    pg.add("popup", key, rect, cols=cols)


def readout(pg, key, rect, px=24):
    lcd(pg, rect)
    pg.add("value", key, rect, px=px, left=True)


# ---------------------------------------------------------------- the common parts


def knob_row(pg, items, x0, y0, w, size=76, px=18, lcd_w=84):
    """a row of parameter knobs, evenly spread over w; items: (key, label); lcd_w widens the value
    windows for knobs whose readout runs long (kHz/ms strings need more than 84)"""
    n = len(items)
    step = w / float(n)
    for i, (key, label) in enumerate(items):
        cx = x0 + step * (i + 0.5)
        knob(pg, key, cx, y0, size, label, lcd_rect=(cx - lcd_w / 2, y0 + size / 2 + 8, lcd_w, 30),
             label_w=min(step - 8, 150), label_px=px)


# ---------------------------------------------------------------- the pages
def osc_page():
    pg = Page("OSC")
    text(pg, (1272, 27), "OSC", 20, INK_DIM, "rm")
    # row 1: SOURCE | OSCILLATOR | the voice-editing keys (a Q-Link press must never fire these, so
    # they stay out of QLINK_BANKS)
    panel(pg, (16, 48, 216, 252), "SOURCE")
    knob(pg, "src_mix", 124, 185, 76, "SRC OSC/NOISE", label_w=200)
    panel(pg, (244, 48, 560, 252), "OSCILLATOR")
    knob_row(pg, [("osc_decay", "OSC DECAY"), ("osc_shape", "OSC SHAPE"), ("osc_xmod", "OSC X-MOD")], 260, 185, 528)
    panel(pg, (816, 48, 448, 252), "VOICE MANAGEMENT")
    momentary(pg, "voice_init", (832, 160, 180, 62), "INIT VOICE", 20)
    momentary(pg, "voice_copy", (1020, 160, 110, 62), "COPY", 20)
    momentary(pg, "voice_paste", (1138, 160, 110, 62), "PASTE", 20)
    text(pg, (1040, 258), "EDITS THE SELECTED VOICE", 16, INK_DIM)
    # row 2: NOISE (moved here from the PITCH page; noise_filter joins the FILTER page)
    panel(pg, (16, 316, 1248, 296), "NOISE")
    knob_row(pg, [("noise_colour", "NOISE COLOUR"), ("noise_attack", "NOISE ATTACK"),
                  ("noise_decay", "NOISE DECAY"), ("noise_shape", "NOISE SHAPE")], 16, 475, 1248)
    return pg


def pitch_page():
    pg = Page("PITCH")
    text(pg, (1272, 27), "PITCH", 20, INK_DIM, "rm")
    # one section per page: its six knobs at full size, centered in the whole page
    panel(pg, (16, 48, 1248, 564), "PITCH")
    knob_row(pg, [("pitch", "PITCH"), ("pitch_track", "KEY TRACK"), ("penv_attack", "P.ENV ATTACK"),
                   ("penv_decay", "P.ENV DECAY"), ("penv_shape", "P.ENV SHAPE"), ("penv_amount", "P.ENV AMOUNT")],
             16, 337, 1248, 86)
    return pg


def filter_page():
    pg = Page("FILTER")
    text(pg, (1272, 27), "FILTER", 20, INK_DIM, "rm")
    # the oscillator filter: mode keys share the title band, its five knobs span the panel
    panel(pg, (16, 48, 724, 272), "OSC FILTER")
    radio(pg, "filter_mode", [(206 + 84 * i, 54, 76, 44) for i in range(3)], gen_params.FILTER_MODES, "key", 17)
    knob_row(pg, [("filter_cutoff", "CUTOFF"), ("filter_res", "RES"), ("fenv_amount", "F.ENV AMOUNT"),
                  ("fenv_decay", "F.ENV DECAY"), ("vel_to_cut", "VEL→CUT")], 32, 204, 700, lcd_w=112)
    # the noise branch's own filter, same shape; its readouts are kHz too
    panel(pg, (16, 336, 724, 272), "NOISE FILTER")
    radio(pg, "nfilt_mode", [(206 + 84 * i, 342, 76, 44) for i in range(3)], gen_params.FILTER_MODES, "key", 17)
    knob_row(pg, [("noise_filter", "NOISE CUTOFF"), ("nfilt_res", "N.FILTER RES"),
                  ("nfilt_env_amount", "N.FENV AMOUNT"), ("nfilt_env_decay", "N.FENV DECAY"),
                  ("nfilt_vel", "N.FILTER VEL")], 32, 492, 700, lcd_w=112)
    # the stutter, stacked in a narrow strip beside them
    panel(pg, (752, 48, 124, 564), "STUTTER")
    knob(pg, "stutter_length", 814, 259, 76, "STUTTER RATE", label_w=112)
    knob(pg, "stutter_strength", 814, 424, 76, "STUTTER STR", label_w=112)
    # the three peaking bands: one row per band (1.0.2: Freq/Gain/Q together), rows at y 170/341/512
    # like the old layout; the freq windows are wide enough for the longest readout ("12.0 kHz")
    panel(pg, (888, 48, 376, 564), "EQ")
    for j, (pk, pre) in enumerate((("eq_", "EQ1 "), ("eq2_", "EQ2 "), ("eq3_", "EQ3 "))):
        knob_row(pg, [(pk + "freq", pre + "FREQ"), (pk + "gain", pre + "GAIN"), (pk + "q", pre + "Q")],
                 888, 170 + 171 * j, 376, lcd_w=112)
    return pg


def fx_page():
    pg = Page("FX")
    text(pg, (1272, 27), "FX", 20, INK_DIM, "rm")
    # the chain in order: dist (drive + tone) -> ring mod -> FM -> comb -> gated reverb
    panel(pg, (16, 48, 240, 191), "DIST")
    knob_row(pg, [("dist_drive", "DRIVE"), ("dist_tone", "TONE")], 16, 155, 240, lcd_w=96)
    panel(pg, (268, 48, 240, 191), "RING MOD")
    knob(pg, "ring_mod", 388, 155, 76, "FREQ", lcd_rect=(340, 201, 96, 30), label_w=216)
    panel(pg, (520, 48, 240, 191), "FM")
    knob(pg, "fm_index", 640, 155, 76, "INDEX", lcd_rect=(592, 201, 96, 30), label_w=216)
    panel(pg, (772, 48, 240, 191), "COMB FLTR")
    knob(pg, "comb", 892, 155, 76, "TIME", lcd_rect=(844, 201, 96, 30), label_w=216)
    panel(pg, (1024, 48, 240, 191), "GATED VERB")
    knob(pg, "gverb", 1144, 155, 76, "WET MIX", lcd_rect=(1096, 201, 96, 30), label_w=216)
    # the delay row: its per-side tempo-sync switches (1.0.2) sit under the title tab, the five knobs
    # below them; the widener (the chain's last stage) takes the right end of the row
    panel(pg, (16, 255, 1000, 357), "DELAY")
    text(pg, (87, 312), "L SYNC")
    radio(pg, "delay_l_sync", [(40, 330, 95, 44), (135, 330, 95, 44)], ["OFF", "ON"], "key", 17)
    text(pg, (307, 312), "R SYNC")
    radio(pg, "delay_r_sync", [(260, 330, 95, 44), (355, 330, 95, 44)], ["OFF", "ON"], "key", 17)
    knob_row(pg, [("delay_detune", "DELAY DETUNE"), ("delay_l", "DELAY L"), ("delay_r", "DELAY R"),
                  ("delay_fb", "DELAY FB"), ("delay_wet", "DELAY WET")], 16, 448, 1000, lcd_w=104)
    panel(pg, (1028, 255, 236, 357), "WIDENER")
    knob(pg, "width", 1146, 448, 112, "WIDTH", lcd_rect=(1098, 512, 96, 30))
    return pg


def lfo_page():
    pg = Page("LFO")
    text(pg, (1272, 27), "LFO", 20, INK_DIM, "rm")
    # three identical panels: sync/retrig keys, the rate and depth knobs, and the three option pickers --
    # fields whose list opens over the page and closes on a pick (docs/NOTES.md's verified mechanism)
    for n in (1, 2, 3):
        x = 16 + 420 * (n - 1)
        panel(pg, (x, 48, 408, 564), "LFO %d" % n)
        text(pg, (x + 36, 112), "SYNC", 16, INK_DIM)
        radio(pg, "lfo%d_sync" % n, [(x + 64, 90, 60, 44), (x + 128, 90, 60, 44)], ["OFF", "ON"], "key", 17)
        text(pg, (x + 224, 112), "RETRIG", 16, INK_DIM)
        radio(pg, "lfo%d_retrig" % n, [(x + 260, 90, 60, 44), (x + 324, 90, 60, 44)], ["OFF", "ON"], "key", 17)
        knob_row(pg, [("lfo%d_rate" % n, "RATE"), ("lfo%d_depth" % n, "DEPTH")], x, 228, 408)
        for j, (word, pkey) in enumerate((("WAVE", "wave"), ("DEST", "dest"), ("DIV", "div"))):
            fit_text(pg, (x + 64, 333 + 94 * j, 280, 22), word, 18, INK_DIM)
            popup(pg, "lfo%d_%s" % (n, pkey), (x + 64, 359 + 94 * j, 280, 44), POPUP_COLS["lfo%d_%s" % (n, pkey)][0])
    return pg


def glb_page():
    pg = Page("CTRL")
    text(pg, (1272, 27), "CTRL", 20, INK_DIM, "rm")
    # CONTROL: the selected voice's navigation plus every velocity-scaled parameter and the base
    # note (the old OSC CONTROL panel moved here with the strip removal)
    panel(pg, (16, 48, 1248, 205), "CONTROL")
    text(pg, (181, 117), "VOICE", 16, INK_DIM)
    momentary(pg, "voice_prev", (40, 137, 54, 62), "", arrow="left")
    readout(pg, "voice_name", (106, 137, 150, 62))
    momentary(pg, "voice_next", (268, 137, 54, 62), "", arrow="right")
    knob_row(pg, [("base_note", "BASE NOTE"), ("vel_to_vol", "VEL→VOL"), ("vel_to_cut", "VEL→CUT"),
                  ("vel_to_noise", "VEL→NOISE"), ("vel_to_osc", "VEL→OSC"), ("vel_to_penv", "VEL→P.ENV")],
             370, 162, 886)
    # KIT / OUTPUT / CHOKE / ABOUT across the bottom row
    panel(pg, (16, 269, 220, 343), "KIT")
    momentary(pg, "kit_init", (40, 397, 172, 62), "KIT INIT", 20)
    text(pg, (126, 499), "RESET ALL 12 VOICES", 16, INK_DIM)
    panel(pg, (248, 269, 380, 343), "OUTPUT")
    radio(pg, "out_sel", [(264, 387 + 48 * i, 100, 44) for i in range(3)], gen_params.OUT_SELS, "key", 17)
    knob_row(pg, [("out_pan", "PAN"), ("out_vol", "VOL")], 380, 452, 220)
    panel(pg, (640, 269, 354, 343), "CHOKE")
    radio(pg, "choke", [(656, 315 + 48 * i, 322, 44) for i in range(5)], gen_params.CHOKE_NAMES, "key", 17)
    text(pg, (817, 570), "RETRIGGERING A VOICE", 15, INK_DIM)
    text(pg, (817, 590), "SILENCES ITS GROUP", 15, INK_DIM)
    panel(pg, (1006, 269, 258, 343), "ABOUT")
    about = ["MPC SYNTHDRUMS", "12-VOICE SYNTH DRUM BOX", "DREAM STATIC SOUNDS",
             "INSPIRED BY DSK SYNTHDRUMS", "NOT AFFILIATED WITH DSK MUSIC", "VERSION 1.0.1"]
    for i, line in enumerate(about):
        fit_text(pg, (1022, 355 + 34 * i, 226, 26), line, 16 if i == 0 else 15, INK if i == 0 else INK_DIM)
    return pg


PAGES = [osc_page, pitch_page, filter_page, fx_page, lfo_page, glb_page]

# The LFO option params open as pickers (design §8; mpc-vst-plugins docs/NOTES.md "popups"): a field bound to
# a hidden "<key>__open" wrapper param that gen_vst's shadow_skin.popup_params() appends after this port's own
# params, in layout order -- so a popup's rank in this list IS its __open index offset (layout_conf() writes
# the popup lines in this order and asserts the pages have not drifted from it). cols/cw size the list: the
# 57 destinations need five columns of 196 px to fit the page, the 12 divisions two, the 7 waves one.
POPUPS = [("lfo1_wave", 1, 280), ("lfo1_dest", 5, 196), ("lfo1_div", 2, 280),
          ("lfo2_wave", 1, 280), ("lfo2_dest", 5, 196), ("lfo2_div", 2, 280),
          ("lfo3_wave", 1, 280), ("lfo3_dest", 5, 196), ("lfo3_div", 2, 280)]
OPEN_INDEX = {key: len(gen_params.PARAMS) + i for i, (key, _, _) in enumerate(POPUPS)}
POPUP_COLS = {key: (cols, cw) for key, cols, cw in POPUPS}


# ---------------------------------------------------------------- Q-Links: each page's knobs, 8 per bank
QLINK_BANKS = {
    "OSC": [["src_mix", "osc_decay", "osc_shape", "osc_xmod"]],
    "PITCH": [["pitch", "pitch_track", "penv_attack", "penv_decay", "penv_shape", "penv_amount"]],
    "FILTER": [["filter_mode", "filter_cutoff", "filter_res", "fenv_amount", "fenv_decay", "vel_to_cut"],
               ["nfilt_mode", "noise_filter", "nfilt_res", "nfilt_env_amount", "nfilt_env_decay", "nfilt_vel"]],
    "FX": [["dist_drive", "dist_tone", "ring_mod", "fm_index", "comb", "gverb", "width", "delay_detune"],
           ["delay_l", "delay_r", "delay_fb", "delay_wet", "delay_l_sync", "delay_r_sync"]],
    "LFO": [["lfo1_rate", "lfo1_depth", "lfo1_wave", "lfo1_dest", "lfo1_div", "lfo2_rate", "lfo2_depth",
             "lfo2_dest"]],
    "CTRL": [["base_note", "vel_to_vol", "vel_to_cut", "vel_to_noise", "vel_to_osc", "vel_to_penv", "out_pan",
              "out_vol"]],
}


def qlink_banks(tab):
    """the Q-Link banks of a tab (its name), 8 slots per bank"""
    return QLINK_BANKS[tab]


def layout_conf():
    L = ["# MPC Synthdrums -- page layout (mpc-vst-plugins layout.conf syntax: tools/shadow_skin.py).",
         "# GENERATED by tools/skin.py --layout: edit that. tools/skin.py then builds every control of the six pages.",
         "theme_bg=121212", "theme_panel=121212", "theme_ink=f0eeea", "theme_ink_dim=bebab6", "theme_accent=f28c1e",
         "theme_knob_ring=f28c1e", "theme_seg_active=f28c1e", ""]
    popups = []
    for name in TABS:
        L.append("[tab %s]" % name)
        banks = qlink_banks(name)
        for i, key in enumerate(k for b in banks for k in b):
            L.append('knob cx=%d cy=%d r=12 label="" key=%s ns=0 bw=30' % (24 + 34 * (i % 36), Y0 + 24 + 40 * (i // 36), key))
        for i, b in enumerate(banks):
            L.append('qlinks "%s" = %s' % (name, ",".join(b)))
        for c in PAGES[TABS.index(name)]().controls:   # popup pickers: their order fixes the __open params' indices
            if c["kind"] == "popup":
                popups.append(c["key"])
                x, y, w, h = c["rect"]
                L.append("popup cx=%d cy=%d w=%d h=%d key=%s cols=%d" % (x + w // 2, y + h // 2, w, h, c["key"],
                                                                        c["cols"]))
        L.append("")
    if popups != [k for k, _, _ in POPUPS]:
        raise SystemExit("skin: the pages' popups drifted from POPUPS (the __open indices depend on the order)")
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------- pictures of the live controls
def knob_strip(cache, size, endless):
    key = ("knob", size, endless)
    if key not in cache:
        pic = caps.knob(size * 2, not endless)
        strip = Image.new("RGBA", (size, size * KNOB_FRAMES), (0, 0, 0, 0))
        for f in range(KNOB_FRAMES):
            ang = -KNOB_SPAN + 2 * KNOB_SPAN * f / (KNOB_FRAMES - 1)
            fr = pic.rotate(-ang, resample=Image.BICUBIC).resize((size, size), Image.LANCZOS)
            strip.paste(fr, (0, f * size))
        cache[key] = strip
    return cache[key]


def cap_pic(cache, colour, w):
    key = ("cap", colour, w)
    if key not in cache:
        body, line = CAP_COLOURS[colour]
        cache[key] = caps.slider(w, round(w * 64 / 48), body, line)
    return cache[key]


def slider_frames(bg, rect, cap, n_frames, horizontal=False):
    x, y, w, h = rect
    under = bg.crop((x, y, x + w, y + h)).convert("RGBA")
    frames = []
    for f in range(n_frames):
        v = f / (n_frames - 1)
        fr = under.copy()
        if horizontal:
            cx = 2 + cap.size[0] / 2 + (w - 4 - cap.size[0]) * v
            fr.alpha_composite(cap, (R(cx - cap.size[0] / 2), (h - cap.size[1]) // 2))
        else:
            cy = h - 2 - cap.size[1] / 2 - (h - 4 - cap.size[1]) * v
            fr.alpha_composite(cap, ((w - cap.size[0]) // 2, R(cy - cap.size[1] / 2)))
        frames.append(fr.convert("RGB"))
    return frames


def key_pics(bg, c):
    """(off, on) pictures of a key: a rounded key, its word, orange when lit"""
    x, y, w, h = c["rect"]
    out = []
    for lit in (False, True):
        im = bg.crop((x, y, x + w, y + h)).convert("RGBA")
        big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        face = c.get("colour", ACCENT) if lit else KEY_FACE
        ink = TAB_INK if lit else INK
        if lit:
            glow = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
            ImageDraw.Draw(glow).rounded_rectangle((2 * SS, 2 * SS, w * SS - 2 * SS, h * SS - 2 * SS), radius=7 * SS,
                                                   fill=ACCENT + (150,))
            big.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
        d.rounded_rectangle((2 * SS, 2 * SS, w * SS - 3 * SS, h * SS - 3 * SS), radius=7 * SS, fill=face + (255,),
                            outline=(140, 136, 132, 255) if not lit else (255, 190, 120, 255), width=2 * SS)
        d.line((10 * SS, 4 * SS, w * SS - 11 * SS, 4 * SS), fill=(255, 255, 255, 70), width=SS)   # the top edge's light
        arrow = c.get("arrow")
        if arrow:
            cx, cy, a = w * SS / 2, h * SS / 2, min(w, h) * SS * 0.22
            pts = {"up": [(cx, cy - a), (cx - a, cy + a * 0.7), (cx + a, cy + a * 0.7)],
                   "down": [(cx, cy + a), (cx - a, cy - a * 0.7), (cx + a, cy - a * 0.7)],
                   "left": [(cx - a, cy), (cx + a * 0.7, cy - a), (cx + a * 0.7, cy + a)],
                   "right": [(cx + a, cy), (cx - a * 0.7, cy - a), (cx - a * 0.7, cy + a)]}[arrow]
            d.polygon(pts, fill=ink + (255,))
        word = c.get("word") or ""
        if word:
            f = font(c.get("px", 16) * SS)
            tw = f.getlength(word)
            if tw > (w - 8) * SS:   # condensed into the key, never smaller
                lay = Image.new("RGBA", (R(tw) + 4, f.size * 2), (0, 0, 0, 0))
                ImageDraw.Draw(lay).text((2, f.size), word, font=f, fill=ink + (255,), anchor="lm")
                lay = lay.resize(((w - 8) * SS, f.size * 2), Image.LANCZOS)
                big.alpha_composite(lay, (4 * SS, R(h * SS / 2 - f.size)))
            else:
                d.text((w * SS / 2, h * SS / 2), word, font=f, fill=ink + (255,), anchor="mm")
        if c.get("chev"):   # a popup field's drop-down affordance, at the right edge
            cx, cy, a = (w - 14) * SS, h * SS / 2, min(w, h) * SS * 0.13
            d.polygon([(cx, cy + a), (cx - a, cy - a), (cx + a, cy - a)], fill=ink + (255,))
        im.alpha_composite(big.resize((w, h), Image.LANCZOS))
        out.append(im)
    return out


def opt_pics(bg, c):
    if c.get("style") == "key":
        return key_pics(bg, c)
    x, y, w, h = c["rect"]
    out = []
    for lit in (False, True):
        im = bg.crop((x, y, x + w, y + h)).convert("RGBA")
        big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        r, cx, cy = 6 * SS, 12 * SS, h * SS / 2
        if lit:
            glow = Image.new("RGBA", big.size, (0, 0, 0, 0))
            ImageDraw.Draw(glow).ellipse((cx - 2 * r, cy - 2 * r, cx + 2 * r, cy + 2 * r), fill=LED_ON + (150,))
            big.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(LED_ON if lit else LED_OFF) + (255,), outline=(20, 8, 8, 255), width=SS)
        f = font(c.get("px", 16) * SS)
        d.text((24 * SS, cy), c["word"], font=f, fill=(INK if lit else INK_DIM) + (255,), anchor="lm")
        im.alpha_composite(big.resize((w, h), Image.LANCZOS))
        out.append(im)
    return out


# ---------------------------------------------------------------- TUI.json pieces (the formats as mpc-vst-plugins
# and LXRXXL write them: docs/NOTES.md of mpc-vst-plugins)
def bounds_s(x, y, w, h):
    return "%d %d %d %d" % (R(x), R(y), R(w), R(h))


def sub(name, typ, data, b, visible="Always"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": visible,
                       "boundsType": "Absolute", "bounds": b, "additionalInvalidatingHandles": []}}


def focus(w, h):
    return sub("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": "00000000",
                                  "backgroundInset": 2.0, "outlineThickness": 0.0}, bounds_s(0, 0, w, h), "WhenFocussed")


def action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": extra, "handle remapping": {"version": 1, "map": []}}


KNOB_ACTIONS = [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
                action("Enter Pressed", "Show Overlay", "knob overlay")]
KEY_ACTIONS = [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
RADIO_ACTIONS = [action("Mouse Down", "Q-Link")]
FIELD_ACTIONS = [action("Mouse Down", "Toggle Switch"), action("Enter Pressed", "Toggle Switch")]


def definition(key, actions, parts, ignore=False):
    return {"key": key, "value": {
        "version": 4, "actions": actions,
        "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
                           "unfocussed": {"version": 1, "colour": "0", "image": ""}},
        "ignoreMousePresses": ignore, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True,
        "componentsData": parts}}


def filmstrip(name, img, frames, w, h, drag="Vertical", x=0, y=0):
    return sub(name, "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": frames,
                              "invert": False, "dragOrientation": drag, "handleName": "Data"}, bounds_s(x, y, w, h))


def button(on_img, off_img, bid, n, w, h):
    return sub("Button", "Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": bid,
                                    "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"},
               bounds_s(0, 0, w, h))


def value_label(w, h, px, colour=LCD_INK, left=False):
    return sub("Value", "Label", {"version": 1, "textStyle": {
        "version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "Bold", "height": px},
        "colour": "ff" + colour,
        "justification": ("left" if left else "horizontallyCentred") + " verticallyCentred", "case": "Original"},
        "type": "Value", "handleName": "Data"}, bounds_s(10 if left else 0, 0, w - (10 if left else 0), h))


def placed(dkey, name, index, rect, focus_on=True, only=None):
    """a placed component on the "Data" handle; only=<param> makes it visible while that parameter is on
    (IndexedEnabling, the mechanism popups and mode panels use: docs/NOTES.md)"""
    x, y, w, h = rect
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": dkey, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % index}]},
            "bounds": {"version": 2, "acceptsHWFocus": "Yes" if focus_on else "No",
                       "showWhenDataModelInvalid": "Show" if only else "Hide",
                       "whenVisible": "Always", "boundsType": "Absolute", "bounds": bounds_s(x, y, w, h),
                       "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter %d" % only] if only else []}}


class Saver:
    def __init__(self, folder):
        self.folder = folder
        self.seen = {}
        self.used = set()

    def save(self, img, name):
        h = (img.mode, img.size, hash(img.tobytes()))
        if h in self.seen:
            return self.seen[h]
        img.save(os.path.join(self.folder, name))
        self.seen[h] = name
        self.used.add(name)
        return name


def build_page(pg, tab, sv, defs, cache):
    kids = []
    on_top = []
    t = "p%d" % tab
    focus_seen = set()

    def place(dkey, name, key, rect):
        f = key not in focus_seen
        focus_seen.add(key)
        kids.append(placed(dkey, name, INDEX[key], rect, f))

    have = {d["key"] for d in defs}

    def add_def(dkey, *a, **kw):
        if dkey not in have:
            defs.append(definition(dkey, *a, **kw))
            have.add(dkey)

    for c in pg.controls:
        x, y, w, h = c["rect"]
        key = c["key"]
        if c["kind"] == "knob":
            strip = knob_strip(cache, w, c["endless"])
            name = sv.save(strip, "sd_knob_%d_%s.png" % (w, "e" if c["endless"] else "p"))
            tb = max(w, 56)
            dkey = "sdKnob_%d_%d_%s" % (w, tb, "e" if c["endless"] else "p")
            add_def(dkey, KNOB_ACTIONS, [focus(tb, tb), filmstrip("Knob", name, KNOB_FRAMES - 1, w, w, "Vertical",
                                                                  (tb - w) // 2, (tb - w) // 2)])
            place(dkey, key, key, (x - (tb - w) // 2, y - (tb - w) // 2, tb, tb))
        elif c["kind"] in ("slider", "hslider"):
            horiz = c["kind"] == "hslider"
            cap = cap_pic(cache, c.get("cap", "black"), c.get("cap_w", 44))
            if horiz:
                cap = cap.rotate(90, expand=True)
            n = min(101, MAX_STRIP // (w if horiz else h)) if not horiz else 101
            frames = slider_frames(pg.bg, c["rect"], cap, n, horiz)
            strip = Image.new("RGB", (w, h * n))
            for i, fr in enumerate(frames):
                strip.paste(fr, (0, i * h))
            name = sv.save(strip, "sd_%s_sl_%s.png" % (t, key))
            dkey = "sdSlider_%s_%s" % (t, key)
            add_def(dkey, KNOB_ACTIONS, [focus(w, h), filmstrip("Slider", name, n, w, h,
                                                                "Horizontal" if horiz else "Vertical")])
            place(dkey, key, key, c["rect"])
        elif c["kind"] == "opt":
            off, on = opt_pics(pg.bg, c)
            on_n = sv.save(on.convert("RGB"), "sd_%s_%s_%d_on.png" % (t, key, c["index"]))
            off_n = sv.save(off.convert("RGB"), "sd_%s_%s_%d_off.png" % (t, key, c["index"]))
            dkey = "sdOpt_%s_%s_%d" % (t, key, c["index"])
            add_def(dkey, RADIO_ACTIONS, [button(on_n, off_n, c["index"], c["count"], w, h)])
            place(dkey, "%s %d" % (key, c["index"]), key, c["rect"])
        elif c["kind"] == "key":
            off, on = key_pics(pg.bg, c)
            on_n = sv.save(on.convert("RGB"), "sd_%s_key_%s_on.png" % (t, key))
            off_n = sv.save(off.convert("RGB"), "sd_%s_key_%s_off.png" % (t, key))
            dkey = "sdKey_%s_%s" % (t, key)
            add_def(dkey, KEY_ACTIONS, [focus(w, h), button(on_n, off_n, 1, 1, w, h)])
            place(dkey, key, key, c["rect"])
        elif c["kind"] == "popup":
            n = len(PBYKEY[key]["options"])
            oi, ei = OPEN_INDEX[key], INDEX[key]
            # the field: a key on the hidden "__open" param (a tap toggles the list; lit while it is open),
            # with the enum's chosen option beside the drop-down chevron
            f = dict(rect=c["rect"], key=key, word="", px=16, colour=ACCENT, chev=True)
            off, on = key_pics(pg.bg, f)
            on_n = sv.save(on.convert("RGB"), "sd_%s_key_%s_on.png" % (t, key))
            off_n = sv.save(off.convert("RGB"), "sd_%s_key_%s_off.png" % (t, key))
            dkey = "sdKey_%s_%s" % (t, key)
            add_def(dkey, FIELD_ACTIONS, [focus(w, h), button(on_n, off_n, 1, 1, w, h)])
            kids.append(placed(dkey, key + " field", oi, c["rect"], True))
            vw = w - 40
            vdkey = "sdValue_%d_%d_24_1" % (vw, h)
            add_def(vdkey, [], [value_label(vw, h, 24, left=True)], ignore=True)
            kids.append(placed(vdkey, key + " value", ei, (x, y, vw, h), False))
            # the list: one radio group on the enum, on top of the page and visible only while the flag is
            # on, so the open list takes the touch over whatever is under it and a pick closes it
            cols, cw = POPUP_COLS[key]
            rows = (n + cols - 1) // cols
            pw = cols * cw + (cols - 1) * 6 + 16
            ph = rows * 30 + (rows - 1) * 4 + 16
            py = y + h + 4 if y + h + 4 + ph <= H - 8 else (y - 4 - ph if y - 4 - ph >= 0 else 0)
            px = max(0, min(x, W - pw))
            shown = "IndexedEnabling/1/2/Parameter %d" % oi
            box = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
            ImageDraw.Draw(box).rounded_rectangle((1, 1, pw - 2, ph - 2), radius=8, fill=ART_BG + (255,),
                                                  outline=ACCENT + (255,), width=2)
            pn = sv.save(box, "sd_pop_%dx%d.png" % (pw, ph))
            panel_kid = sub("%s list" % key, "Image", {"version": 2, "imageType": "Regular", "colour": "0",
                                                       "image": pn}, bounds_s(px, py, pw, ph))
            panel_kid["bounds"]["additionalInvalidatingHandles"] = [shown]
            on_top.append(panel_kid)
            words = PBYKEY[key]["options"]
            for o in range(n):
                ox = px + 8 + (o % cols) * (cw + 6)
                oy = py + 8 + (o // cols) * 34
                oc = dict(rect=(ox, oy, cw, 30), key=key, index=o, count=n, word=words[o], style="key", px=15,
                          colour=ACCENT)
                ooff, oon = opt_pics(pg.bg, oc)
                oo_n = sv.save(oon.convert("RGB"), "sd_%s_pop_%s_%d_on.png" % (t, key, o))
                of_n = sv.save(ooff.convert("RGB"), "sd_%s_pop_%s_%d_off.png" % (t, key, o))
                odkey = "sdPop_%s_%s_%d" % (t, key, o)
                add_def(odkey, RADIO_ACTIONS, [button(oo_n, of_n, o, n, cw, 30)])
                on_top.append(placed(odkey, "popup %s %s" % (key, words[o]), ei, (ox, oy, cw, 30), False, only=oi))
        elif c["kind"] == "value":
            px = c.get("px", 24)
            dkey = "sdValue_%d_%d_%d_%d" % (w, h, px, 1 if c.get("left") else 0)
            add_def(dkey, [], [value_label(w, h, px, left=c.get("left", False))], ignore=True)
            kids.append(placed(dkey, key + " value", INDEX[key], c["rect"], False))
    return kids + on_top


def build(skin):
    tui_path = os.path.join(skin, "TUI.json")
    tui = json.load(open(tui_path))
    cdefs = tui["pageData"]["componentDefinitions"]
    defs = cdefs["localComponentDefinitions"]
    sv = Saver(skin)
    cache = {}
    newdefs, pages = [], {}
    for tab, make in enumerate(PAGES):
        pg = make()
        kids = build_page(pg, tab, sv, newdefs, cache)
        bgname = sv.save(pg.bg, "sd_bg_%d.png" % tab)
        pages[TABS[tab]] = [sub("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname},
                                bounds_s(0, 0, W, H))] + kids
        # every Q-Link key has a control on its page
        bound = {k["handle remapping"]["map"][0]["value"] for k in kids if k["handle remapping"]["map"]}
        for b in qlink_banks(TABS[tab]):
            for key in b:
                if "Parameter %d" % INDEX[key] not in bound:
                    raise SystemExit("skin: %s (a Q-Link of %s) has no control on the page" % (key, TABS[tab]))
    keep = []
    for e in defs:
        tabname = e["key"].split("|")[0]
        if "|" in e["key"] and tabname in pages:
            e["value"]["componentsData"] = pages[tabname]
            e["value"]["backgroundData"] = {"version": 1, "focussed": {"version": 1, "colour": "ff000000", "image": ""},
                                            "unfocussed": {"version": 1, "colour": "ff000000", "image": ""}}
            keep.append(e)
    cdefs["localComponentDefinitions"] = keep + newdefs
    json.dump(tui, open(tui_path, "w"), indent=1)
    for f in os.listdir(skin):
        if f.endswith(".png") and f not in sv.used and not f.startswith("browser"):
            os.remove(os.path.join(skin, f))
    print("skin: %d pages, %d definitions, %d pictures" % (len(pages), len(newdefs), len(sv.used)))


# ---------------------------------------------------------------- the offline preview
def default_norm(key):
    p = PBYKEY[key]
    if p.get("options"):
        return p["default"] / max(1, len(p["options"]) - 1)
    lo, hi = p.get("min", 0), p.get("max", 1)
    return (p["default"] - lo) / (hi - lo) if hi > lo else 0


def default_text(key):
    p = PBYKEY[key]
    if key == "voice_name":
        return gen_params.VOICE_NAMES[PBYKEY["voice"]["default"]]
    if p.get("options"):
        return p["options"][p["default"]]
    return str(p["default"])


def preview(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    cache = {}
    for tab, make in enumerate(PAGES):
        pg = make()
        img = pg.bg.convert("RGBA")
        d = ImageDraw.Draw(img)
        for c in pg.controls:
            x, y, w, h = c["rect"]
            key = c["key"]
            n = default_norm(key)
            if c["kind"] == "knob":
                strip = knob_strip(cache, w, c["endless"])
                f = R(n * (KNOB_FRAMES - 1))
                img.alpha_composite(strip.crop((0, f * w, w, f * w + w)), (x, y))
            elif c["kind"] in ("slider", "hslider"):
                horiz = c["kind"] == "hslider"
                cap = cap_pic(cache, c.get("cap", "black"), c.get("cap_w", 44))
                if horiz:
                    cap = cap.rotate(90, expand=True)
                frames = slider_frames(pg.bg, c["rect"], cap, 101, horiz)
                img.paste(frames[R(n * 100)], (x, y))
            elif c["kind"] == "opt":
                off, on = opt_pics(pg.bg, c)
                sel = R(n * (c["count"] - 1)) == c["index"]
                img.alpha_composite(on if sel else off, (x, y))
            elif c["kind"] == "key":
                off, on = key_pics(pg.bg, c)
                img.alpha_composite(on if PBYKEY[key].get("default", 0) and PBYKEY[key].get("options") else off, (x, y))
            elif c["kind"] == "popup":   # the field, closed: the list only exists on the device (IndexedEnabling)
                off, on = key_pics(pg.bg, dict(rect=c["rect"], word="", px=16, colour=ACCENT, chev=True))
                img.alpha_composite(off, (x, y))
                f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", R(24 * 0.72))
                d.text((x + 10, y + h / 2), default_text(key), font=f, fill="#" + LCD_INK, anchor="lm")
            elif c["kind"] == "value":
                s = default_text(key)
                f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", R(c.get("px", 24) * 0.72))
                if c.get("left"):
                    d.text((x + 10, y + h / 2), s, font=f, fill="#" + LCD_INK, anchor="lm")
                else:
                    d.text((x + w / 2, y + h / 2), s, font=f, fill="#" + LCD_INK, anchor="mm")
        img.convert("RGB").save(os.path.join(out_dir, "page_%d_%s.png" % (tab, TABS[tab].replace("/", "_").replace(" ", "_"))))
    print("preview: %s" % out_dir)


if __name__ == "__main__":
    if "--layout" in sys.argv:
        open(os.path.join(ROOT, "layout.conf"), "w").write(layout_conf())
        print("layout.conf written")
    elif "--check" in sys.argv:
        if open(os.path.join(ROOT, "layout.conf")).read() != layout_conf():
            raise SystemExit("layout.conf is stale: run python3 tools/skin.py --layout")
    elif "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])
    else:
        build(sys.argv[1])
