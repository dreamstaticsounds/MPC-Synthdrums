#!/usr/bin/env python3
"""MPC Synthdrums' tile in MPC's Sounds > INSTRUMENTS browser (270 x 110): the oscillator's display with the name."""
import os
import sys
from PIL import Image, ImageDraw, ImageEnhance

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin  # noqa: E402

pg = skin.osc_page()
img = pg.bg.crop((600, 300, 600 + 540, 300 + 220)).resize((270, 110), Image.LANCZOS)
img = ImageEnhance.Brightness(img).enhance(0.55)
d = ImageDraw.Draw(img)
d.rounded_rectangle((8, 22, 262, 88), radius=10, fill=(18, 18, 18), outline=skin.ACCENT, width=2)
title, sub = "MPC SYNTHDRUMS", "12-VOICE SYNTH DRUM BOX"
px = 30                      # the largest size whose title still fits the frame
while px > 14 and skin.font(px).getlength(title) > 240:
    px -= 1
d.text((135, 48), title, font=skin.font(px), fill=skin.ACCENT, anchor="mm")
d.text((135, 76), sub, font=skin.font(16), fill=skin.INK, anchor="mm")
img.save(os.path.join(skin.ROOT, "art", "tile.png"))
print("art/tile.png")
