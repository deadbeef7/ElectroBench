#!/usr/bin/env python3
"""Coarse ASCII view of a render. There is no way to look at a PPM in this
terminal, so print it: one character per block, glyph chosen by luminance
band and hue. Also prints per-band means so numbers can be checked without
guessing from the art.

Usage: python3 scripts/ascii_view.py frame.ppm [cols] [rows]
"""
import sys


def load(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


GLYPH = " .:-=+*#%@"  # dark -> bright


def hue_char(r, g, b):
    mx, mn = max(r, g, b), min(r, g, b)
    if mx <= 0.02:
        return " "
    sat = (mx - mn) / mx
    if sat < 0.10:
        return "W" if mx > 0.72 else ("w" if mx > 0.40 else "_")
    if r >= g and r >= b:
        return "R" if sat > 0.45 else "r"
    if g >= r and g >= b:
        return "G" if sat > 0.45 else "g"
    return "B" if sat > 0.45 else "b"


def main():
    path = sys.argv[1]
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 110
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else 34
    w, h, px = load(path)
    print("== %s  %dx%d ==" % (path, w, h))
    for ry in range(rows):
        line = ""
        y0, y1 = ry * h // rows, (ry + 1) * h // rows
        for rx in range(cols):
            x0, x1 = rx * w // cols, (rx + 1) * w // cols
            n = 0
            r = g = b = 0.0
            for y in range(y0, max(y1, y0 + 1), 2):
                for x in range(x0, max(x1, x0 + 1)):
                    i = (y * w + x) * 3
                    r += px[i]
                    g += px[i + 1]
                    b += px[i + 2]
                    n += 1
            r, g, b = r / n / 255.0, g / n / 255.0, b / n / 255.0
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            mx, mn = max(r, g, b), min(r, g, b)
            sat = (mx - mn) / mx if mx > 0 else 0
            # hue letter when the pixel is saturated enough to read as colour,
            # otherwise a luminance ramp
            if sat > 0.22:
                line += hue_char(r, g, b)
            else:
                line += GLYPH[min(9, int(luma * 10))]
        print(line)


main()
