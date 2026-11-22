"""Draws the small flags shown next to each language in the Language menu: res/flag_<code>.bmp,
24 x 12 pixels, 24-bit (the format of the existing flag_ru.bmp, which is kept as it is). Needs Python 3
and Pillow. Each flag is drawn 8x larger and scaled down for smooth edges; at menu size only the main
shapes and colours matter, so emblems are simplified.

    python res/make_flags.py

Language code -> country: en United Kingdom, pt-BR Brazil, ar Saudi Arabia, fa Iran, zh-TW Taiwan,
the others their own country. Resource names: FLAG_<CODE> with "-" as "_" (FLAG_PT_BR), see app.rc.
"""
import math
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 24, 12
S = 8  # supersampling
BW, BH = W * S, H * S


def new(color=(255, 255, 255)):
    im = Image.new("RGB", (BW, BH), color)
    return im, ImageDraw.Draw(im)


def hstripes(colors, weights=None):
    im, d = new()
    weights = weights or [1] * len(colors)
    total, y = sum(weights), 0.0
    for c, w in zip(colors, weights):
        h = BH * w / total
        d.rectangle([0, round(y), BW, round(y + h)], fill=c)
        y += h
    return im, d


def vstripes(colors):
    im, d = new()
    w = BW / len(colors)
    for i, c in enumerate(colors):
        d.rectangle([round(i * w), 0, round((i + 1) * w), BH], fill=c)
    return im, d


def star(d, cx, cy, r, color, points=5, rot=-90, inner=0.382):
    pts = []
    for i in range(points * 2):
        rr = r if i % 2 == 0 else r * inner
        a = math.radians(rot + i * 180 / points)
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=color)


def uk():
    blue, red, white = (1, 33, 105), (200, 16, 46), (255, 255, 255)
    im, d = new(blue)
    for w, c in ((BH * 0.15, white), (BH * 0.05, red)):
        d.line([0, 0, BW, BH], fill=c, width=round(w))
        d.line([0, BH, BW, 0], fill=c, width=round(w))
    d.rectangle([BW / 2 - BH * 0.17, 0, BW / 2 + BH * 0.17, BH], fill=white)
    d.rectangle([0, BH / 2 - BH * 0.17, BW, BH / 2 + BH * 0.17], fill=white)
    d.rectangle([BW / 2 - BH * 0.1, 0, BW / 2 + BH * 0.1, BH], fill=red)
    d.rectangle([0, BH / 2 - BH * 0.1, BW, BH / 2 + BH * 0.1], fill=red)
    return im


def spain():
    im, d = hstripes([(170, 21, 27), (241, 191, 0), (170, 21, 27)], [1, 2, 1])
    d.rectangle([BW * 0.22, BH * 0.33, BW * 0.32, BH * 0.67], fill=(170, 21, 27))  # simplified arms
    return im


def sweden():
    blue, yellow = (0, 106, 167), (254, 204, 0)
    im, d = new(blue)
    d.rectangle([BW * 0.3, 0, BW * 0.42, BH], fill=yellow)
    d.rectangle([0, BH * 0.4, BW, BH * 0.6], fill=yellow)
    return im


def vietnam():
    im, d = new((218, 37, 29))
    star(d, BW / 2, BH / 2 + BH * 0.03, BH * 0.33, (255, 255, 0))
    return im


def turkey():
    red, white = (227, 10, 23), (255, 255, 255)
    im, d = new(red)
    cx, cy, r = BW * 0.36, BH / 2, BH * 0.25
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=white)
    r2 = r * 0.8
    d.ellipse([cx + r * 0.25 - r2, cy - r2, cx + r * 0.25 + r2, cy + r2], fill=red)
    star(d, BW * 0.52, cy, BH * 0.12, white, rot=180)
    return im


def brazil():
    green, yellow, blue = (0, 156, 59), (255, 223, 0), (0, 39, 118)
    im, d = new(green)
    m = BH * 0.1
    d.polygon([(m * 1.7, BH / 2), (BW / 2, m), (BW - m * 1.7, BH / 2), (BW / 2, BH - m)], fill=yellow)
    r = BH * 0.24
    d.ellipse([BW / 2 - r, BH / 2 - r, BW / 2 + r, BH / 2 + r], fill=blue)
    return im


def israel():
    blue = (0, 56, 184)
    im, d = new()
    d.rectangle([0, BH * 0.1, BW, BH * 0.25], fill=blue)
    d.rectangle([0, BH * 0.75, BW, BH * 0.9], fill=blue)
    cx, cy, r = BW / 2, BH / 2, BH * 0.2
    for rot in (-90, 90):
        pts = [(cx + r * math.cos(math.radians(rot + k * 120)), cy + r * math.sin(math.radians(rot + k * 120))) for k in range(3)]
        d.polygon(pts, outline=blue, width=S)
    return im


def saudi():
    green, white = (0, 108, 53), (255, 255, 255)
    im, d = new(green)
    d.rectangle([BW * 0.25, BH * 0.3, BW * 0.75, BH * 0.45], fill=white)  # the shahada, simplified
    d.rectangle([BW * 0.28, BH * 0.62, BW * 0.72, BH * 0.68], fill=white)  # the sword
    return im


def iran():
    im, d = hstripes([(35, 159, 64), (255, 255, 255), (218, 0, 0)])
    r = BH * 0.12
    d.ellipse([BW / 2 - r, BH / 2 - r, BW / 2 + r, BH / 2 + r], fill=(218, 0, 0))  # emblem, simplified
    return im


def thailand():
    red, white, blue = (165, 25, 49), (255, 255, 255), (45, 42, 74)
    im, _ = hstripes([red, white, blue, white, red], [1, 1, 2, 1, 1])
    return im


def japan():
    im, d = new()
    r = BH * 0.3
    d.ellipse([BW / 2 - r, BH / 2 - r, BW / 2 + r, BH / 2 + r], fill=(188, 0, 45))
    return im


def china():
    yellow = (255, 222, 0)
    im, d = new((238, 28, 37))
    star(d, BW * 0.17, BH * 0.3, BH * 0.18, yellow)
    for x, y in ((0.33, 0.12), (0.4, 0.24), (0.4, 0.42), (0.33, 0.54)):
        star(d, BW * x, BH * y, BH * 0.06, yellow)
    return im


def taiwan():
    red, blue, white = (254, 0, 0), (0, 0, 149), (255, 255, 255)
    im, d = new(red)
    d.rectangle([0, 0, BW / 2, BH / 2], fill=blue)
    cx, cy = BW / 4, BH / 4
    star(d, cx, cy, BH * 0.19, white, points=12, inner=0.6)
    r = BH * 0.09
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=white, outline=blue, width=S // 2)
    return im


def korea():
    red, blue, black = (205, 46, 58), (0, 71, 160), (0, 0, 0)
    im, d = new()
    cx, cy, r = BW / 2, BH / 2, BH * 0.25
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 180, 360, fill=red)
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 0, 180, fill=blue)
    h = r / 2
    d.ellipse([cx - r, cy - h, cx, cy + h], fill=red)
    d.ellipse([cx, cy - h, cx + r, cy + h], fill=blue)
    for x, y in ((0.17, 0.22), (0.83, 0.22), (0.17, 0.78), (0.83, 0.78)):  # trigrams, simplified
        d.rectangle([BW * x - BH * 0.09, BH * y - BH * 0.07, BW * x + BH * 0.09, BH * y + BH * 0.07], fill=black)
    return im


FLAGS = {
    "en": uk,
    "de": lambda: hstripes([(0, 0, 0), (221, 0, 0), (255, 206, 0)])[0],
    "es": spain,
    "fr": lambda: vstripes([(0, 35, 149), (255, 255, 255), (237, 41, 57)])[0],
    "it_it": lambda: vstripes([(0, 146, 70), (255, 255, 255), (206, 43, 55)])[0],
    "hu": lambda: hstripes([(206, 41, 57), (255, 255, 255), (71, 112, 80)])[0],
    "pl": lambda: hstripes([(255, 255, 255), (220, 20, 60)])[0],
    "pt_br": brazil,
    "sv": sweden,
    "vi": vietnam,
    "tr": turkey,
    "uk": lambda: hstripes([(0, 87, 183), (255, 215, 0)])[0],
    "he": israel,
    "ar": saudi,
    "fa": iran,
    "th": thailand,
    "ja": japan,
    "zh_cn": china,
    "zh_tw": taiwan,
    "ko": korea,
}

if __name__ == "__main__":
    for code, draw in FLAGS.items():
        im = draw().resize((W, H), Image.LANCZOS)
        im.save(os.path.join(HERE, f"flag_{code}.bmp"))
        print(f"flag_{code}.bmp")
