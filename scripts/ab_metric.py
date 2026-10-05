#!/usr/bin/env python3
"""A/B analyzer: speckle "pop" pixels vs rich sparkle, for tide PPMs.

pop  = pixel >=0.45 lum standing >=1.8x above its 5x5 local median
       (the isolated bright dots the user circles in screenshots)
rich = pixels >0.62 lum in the sea half (sparkle richness must survive)

Usage: python3 scripts/ab_metric.py a.ppm b.ppm ...
"""
import sys


def read_ppm(p):
    d = open(p, "rb").read()
    f, i = [], 2
    while len(f) < 3:
        while d[i:i + 1].isspace():
            i += 1
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        f.append(int(d[i:j]))
        i = j
    while i < len(d) and d[i:i + 1].isspace():
        i += 1
    w, h, mx = f
    px = d[i:i + w * h * 3]
    assert len(px) == w * h * 3, (p, len(px), w * h * 3)
    return w, h, px


def analyze(p):
    w, h, px = read_ppm(p)

    def lum(x, y):
        o = (y * w + x) * 3
        return (0.2126 * px[o] + 0.7152 * px[o + 1] + 0.0722 * px[o + 2]) / 255.0

    horizon = int(h * 0.52)
    pops, rich = 0, 0
    for y in range(horizon, h - 2):
        for x in range(2, w - 2):
            v = lum(x, y)
            if v < 0.45:
                continue
            nb = []
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    if dx or dy:
                        nb.append(lum(x + dx, y + dy))
            nb.sort()
            med = nb[12]
            if med > 0 and v / max(med, 1e-4) > 1.8:
                pops += 1
    for y in range(horizon, h - 2):
        for x in range(0, w):
            if lum(x, y) > 0.62:
                rich += 1
    mean = sum(lum(x, y) for y in range(0, h, 3) for x in range(0, w, 3)) / ((h // 3) * (w // 3))
    return pops, rich, mean


for p in sys.argv[1:]:
    po, ri, me = analyze(p)
    print(f"{p}: pops={po} rich(>0.62)={ri} mean={me:.4f}")
