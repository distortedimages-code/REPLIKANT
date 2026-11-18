#!/usr/bin/env python3
"""REPLIK8. Draws the DMG window's background (tools/dmg/background.png and background@2x.png): cream paper, the
black of the ink, the red of the badge's band. Light because Finder writes the icon names in black.
The icon places it is drawn around are those of dmg_settings.py (keep them together).
Needs Pillow. Run: python3 tools/dmg/draw_background.py"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', 'Resources', 'fonts')
W, H = 720, 520
PAPER, INK, MID, LINE, RED, CREAM = '#ECE4D4', '#121312', '#6F675B', '#C9BFAE', '#AD0013', '#ECE4D4'
PKG, README = (150, 235), (150, 420)
ROWS = [(420, 180, 610, 'AU'), (420, 290, 610, 'VST3'), (420, 400, 610, 'APPLI')]


def draw(s):
    im = Image.new('RGB', (W * s, H * s), PAPER)
    d = ImageDraw.Draw(im)
    S = lambda v: int(round(v * s))
    bold = lambda size: ImageFont.truetype(os.path.join(FONTS, 'SpaceGrotesk-Bold.ttf'), S(size))
    med = lambda size: ImageFont.truetype(os.path.join(FONTS, 'SpaceGrotesk-Medium.ttf'), S(size))

    # the badge: REPLIK8 over a red band that says SAMPLER
    bx, by, bw, bh = 36, 22, 196, 58
    d.rounded_rectangle((S(bx), S(by), S(bx + bw), S(by + bh)), radius=S(9), fill=PAPER, outline=INK, width=S(3))
    band = by + bh * 0.62
    d.rounded_rectangle((S(bx), S(band), S(bx + bw), S(by + bh)), radius=S(9), fill=RED, outline=INK, width=S(3))
    d.rectangle((S(bx + 2), S(band), S(bx + bw - 2), S(band + 8)), fill=RED)
    d.line((S(bx), S(band), S(bx + bw), S(band)), fill=INK, width=S(3))
    name = bold(30)
    word = Image.new('L', (S(400), S(60)), 0)
    ImageDraw.Draw(word).text((0, 0), 'REPLIK8', font=name, fill=255)
    box = word.getbbox()
    word = word.crop(box)
    word = word.resize((int(word.width * 1.22), word.height))
    top_h = (band - by)
    im.paste(Image.new('RGB', word.size, INK), (S(bx + bw / 2) - word.width // 2, S(by + top_h / 2) - word.height // 2 + S(1)), word)
    tag = med(10)
    spaced = '  '.join('SAMPLER')
    d.text((S(bx + bw / 2) - d.textlength(spaced, font=tag) / 2, S(band + (by + bh - band) / 2 - 7)), spaced, font=tag, fill=CREAM)
    d.text((S(bx + bw + 18), S(by + 12)), 'synthé · sampler · autosampleur', font=med(15), fill=INK)
    d.text((S(bx + bw + 18), S(by + 34)), 'quitte ton logiciel de musique avant d\'installer', font=med(12), fill=MID)
    d.line((S(24), S(96), S(W - 24), S(96)), fill=LINE, width=S(1))

    def panel(x0, y0, x1, y1, title):
        d.rounded_rectangle((S(x0), S(y0), S(x1), S(y1)), radius=S(12), outline=INK, width=S(2))
        d.text((S(x0 + 14), S(y0 + 10)), title, font=med(11), fill=RED)

    # left: the installer
    panel(36, 116, 264, 484, 'LE PLUS SIMPLE')
    txt = med(15)
    d.text((S(PKG[0]) - d.textlength('double-clic', font=txt) / 2, S(PKG[1] - 84)), 'double-clic', font=txt, fill=INK)
    d.polygon([(S(PKG[0] - 7), S(PKG[1] - 60)), (S(PKG[0] + 7), S(PKG[1] - 60)), (S(PKG[0]), S(PKG[1] - 50))], fill=RED)
    small = med(12)
    d.text((S(PKG[0]) - d.textlength('macOS refuse ?', font=small) / 2, S(README[1] - 76)), 'macOS refuse ?', font=small, fill=MID)
    d.text((S(PKG[0]) - d.textlength('la commande est ici', font=small) / 2, S(README[1] - 60)), 'la commande est ici', font=small, fill=MID)

    # right: by hand
    panel(290, 116, 690, 484, 'OU GLISSE CHAQUE ÉLÉMENT SUR SON DOSSIER')
    for fx, fy, tx, label in ROWS:
        for xx in range(S(fx + 46), S(tx - 54), S(8)):
            d.rectangle((xx, S(fy - 1), xx + S(4), S(fy + 1)), fill=INK)
        d.polygon([(S(tx - 54), S(fy - 6)), (S(tx - 54), S(fy + 6)), (S(tx - 46), S(fy))], fill=INK)
        d.text((S(305), S(fy - 8)), label, font=med(12), fill=RED)
    d.text((S(304), S(456)), 'puis la commande du LISEZ-MOI dans le Terminal', font=small, fill=MID)
    return im


draw(1).save(os.path.join(HERE, 'background.png'))
draw(2).save(os.path.join(HERE, 'background@2x.png'))
print('drawn')
