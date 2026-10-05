#!/usr/bin/env python3
# Horizon-band blue-mass metric for the splash de-cloud A/B (ad-hoc, untracked).
# Detects the waterline (strongest horizontal luminance step in the lower
# half) and measures the BLUE-COTTON mass only ABOVE it, where the dome
# checker and the splash film live.
# Usage: python3 scripts/band_ab.py /tmp/x/*.ppm
import sys

def load_ppm(p):
    with open(p, 'rb') as f:
        data = f.read()
    idx = 0
    def tok():
        nonlocal idx
        while True:
            while idx < len(data) and data[idx:idx+1].isspace():
                idx += 1
            if data[idx:idx+1] == b'#':
                while idx < len(data) and data[idx:idx+1] not in (b'\n', b''):
                    idx += 1
            else:
                break
        s = idx
        while idx < len(data) and not data[idx:idx+1].isspace():
            idx += 1
        return data[s:idx]
    assert tok() == b'P6'
    w = int(tok()); h = int(tok()); int(tok())
    idx += 1
    return w, h, data[idx:idx + w * h * 3]

def lum(px, w, x, y):
    o = (y * w + x) * 3
    return 0.299 * px[o] + 0.587 * px[o + 1] + 0.114 * px[o + 2]

def find_waterline(px, w, h):
    # strongest negative luminance step scanning down the centre band
    best_y, best_step = h // 2, 0.0
    x0, x1 = w // 3, 2 * w // 3
    for y in range(h // 3, 5 * h // 6):
        a = sum(lum(px, w, x, y - 2) for x in range(x0, x1, 8)) / ((x1 - x0) // 8)
        b = sum(lum(px, w, x, y + 2) for x in range(x0, x1, 8)) / ((x1 - x0) // 8)
        if a - b > best_step:
            best_step, best_y = a - b, y
    return best_y

def blue_pct_above(px, w, h, y1):
    blue = tot = 0
    for y in range(0, min(y1, h)):
        row = y * w * 3
        for x in range(w * 2 // 5, w * 4 // 5):   # central band, avoids HUD corners
            o = row + x * 3
            r, g, b = px[o], px[o + 1], px[o + 2]
            if b > r * 1.08 and b > g * 1.05 and b > 40:
                blue += 1
            tot += 1
    return 100.0 * blue / max(tot, 1)

for f in sorted(sys.argv[1:]):
    w, h, px = load_ppm(f)
    wl = find_waterline(px, w, h)
    pct = blue_pct_above(px, w, h, max(wl - 12, 0))
    print(f"{f}: waterline y={wl}  blue-above={pct:5.2f}%")
