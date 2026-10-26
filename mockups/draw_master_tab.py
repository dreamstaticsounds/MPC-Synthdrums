#!/usr/bin/env python3
"""Throwaway mockup (not product code): draws the proposed Synthdrums 1.1.0 MASTER tab as a JPG.

    python3 mockups/draw_master_tab.py       -> mockups/master-tab.jpg

Reuses tools/skin.py's drawing helpers against stand-in parameters for the 33 controls that
1.1.0 will add (12 faders + 12 pan knobs + the 9 master-EQ knobs), so the picture shows the
real theme, real knob/fader art and real fonts. Geometry here is a proposal for review."""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import skin  # noqa: E402

# ---- stand-in parameters for keys that don't exist until 1.1.0 is built --------------------------
_fake = []
for i in range(1, 13):
    _fake.append(("mix_vol_%d" % i, {"min": 0, "max": 127, "default": 100, "display": "int"}))
    _fake.append(("mix_pan_%d" % i, {"min": -64, "max": 63, "default": 0, "display": "int"}))
for suf, lo, hi, d in (("hi_freq", 0, 127, 64), ("hi_gain", -63, 63, 0), ("hi_q", 0, 127, 64),
                       ("freq", 0, 127, 64), ("gain", -63, 63, 0), ("q", 0, 127, 64),
                       ("lo_freq", 0, 127, 64), ("lo_gain", -63, 63, 0), ("lo_q", 0, 127, 64)):
    _fake.append(("meq_" + suf, {"min": lo, "max": hi, "default": d, "display": "int",
                                 "dynamic_display": bool("freq" in suf)}))
for n, (k, d) in enumerate(_fake):
    skin.PBYKEY[k] = d
    skin.INDEX.setdefault(k, 5000 + n)


def master_page():
    """the proposed MASTER tab: MIXER (12 faders + 12 pans, two rows of six) | MASTER EQ"""
    pg = skin.Page("MASTER")
    skin.text(pg, (1272, 27), "MASTER", 20, skin.INK_DIM, "rm")
    # MIXER: six columns, two rows; a small PAN knob above each fader (mixer-scale: 36 px, 12 px
    # type), both readouts the compact 44x20 window, the freed height going to the fader's travel
    # (caps stay the standard 44 px), the fader labelled by voice number
    skin.panel(pg, (16, 48, 856, 564), "MIXER")
    step = 824 / 6.0
    rows = ((94, 128, 160, 185, 345),   # row 1: PAN label y, pan knob cy, pan lcd y, fader top/bottom
            (358, 392, 424, 449, 607))  # row 2
    for row, (lab, pcy, ply, ftop, fbot) in enumerate(rows):
        for col in range(6):
            n = row * 6 + col + 1
            cx = 32 + step * (col + 0.5)
            skin.fit_text(pg, (cx - 50, lab - 8, 100, 16), "PAN", 12, skin.INK)
            skin.knob(pg, "mix_pan_%d" % n, cx, pcy, 36, None, lcd_rect=False)
            skin.lcd(pg, (cx - 22, ply, 44, 20))
            pg.add("value", "mix_pan_%d" % n, (cx - 22, ply, 44, 20), px=13)
            skin.fit_text(pg, (cx - 32, ftop, 64, 20), str(n), 16, skin.INK)
            skin.lcd(pg, (cx - 22, ftop + 22, 44, 20))
            pg.add("value", "mix_vol_%d" % n, (cx - 22, ftop + 22, 44, 20), px=13)
            y0 = ftop + 47
            skin.slot(pg, cx, y0, fbot, False)
            pg.add("slider", "mix_vol_%d" % n, (cx - 30, y0 - 2, 60, fbot - y0 + 4),
                   cap="white", cap_w=44)
    # MASTER EQ: three rows like the Filter tab's EQ -- high shelf / parametric / low shelf
    skin.panel(pg, (888, 48, 376, 564), "MASTER EQ")
    for j, (pre, words) in enumerate((("meq_hi_", ("HIGH FREQ", "HIGH GAIN", "HIGH Q")),
                                      ("meq_", ("EQ FREQ", "EQ GAIN", "EQ Q")),
                                      ("meq_lo_", ("LOW FREQ", "LOW GAIN", "LOW Q")))):
        skin.knob_row(pg, [(pre + s, w) for s, w in zip(("freq", "gain", "q"), words)],
                      888, 170 + 171 * j, 376, lcd_w=112)
    return pg


def lcd_font(px):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", skin.FONT):
        if os.path.exists(path):
            return ImageFont.truetype(path, int(px * 0.72))
    return None


def render(pg, out_path):
    """tools/skin.py's preview loop, over one page, saved as a JPG"""
    cache = {}
    img = pg.bg.convert("RGBA")
    d = ImageDraw.Draw(img)
    for c in pg.controls:
        x, y, w, h = c["rect"]
        key = c["key"]
        n = skin.default_norm(key)
        if c["kind"] == "knob":
            strip = skin.knob_strip(cache, w, c["endless"])
            f = skin.R(n * (skin.KNOB_FRAMES - 1))
            img.alpha_composite(strip.crop((0, f * w, w, f * w + w)), (x, y))
        elif c["kind"] == "slider":
            cap = skin.cap_pic(cache, c.get("cap", "black"), c.get("cap_w", 44))
            frames = skin.slider_frames(pg.bg, c["rect"], cap, 101)
            img.paste(frames[skin.R(n * 100)], (x, y))
        elif c["kind"] == "value":
            f = lcd_font(c.get("px", 24))
            s = skin.default_text(key)
            if c.get("left"):
                d.text((x + 10, y + h / 2), s, font=f, fill="#" + skin.LCD_INK, anchor="lm")
            else:
                d.text((x + w / 2, y + h / 2), s, font=f, fill="#" + skin.LCD_INK, anchor="mm")
    img.convert("RGB").save(out_path, "JPEG", quality=92)
    print(out_path)


if __name__ == "__main__":
    os.makedirs(HERE, exist_ok=True)
    render(master_page(), os.path.join(HERE, "master-tab.jpg"))
