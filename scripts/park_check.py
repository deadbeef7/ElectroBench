#!/usr/bin/env python3
# Parked-pot visibility metric (ad-hoc, untracked).
# Counts bright near-white pixels in the near-water band (rows 55-92% of the
# frame), where a teapot parked at its fall point shows its ceramic hull.
# Wave-one quiet window (t ~ 9.5-11 s): D4 pots sank (invisible) -> ~0;
# D5 pots park at the surface -> a clear count.
# Usage: python3 scripts/park_check.py /tmp/x5/*.ppm
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

for f in sorted(sys.argv[1:]):
    w, h, px = load_ppm(f)
    bright = tot = 0
    for y in range(int(0.55 * h), int(0.92 * h)):
        row = y * w * 3
        for x in range(0, w, 2):
            o = row + x * 3
            r, g, b = px[o], px[o + 1], px[o + 2]
            if min(r, g, b) >= 170 and max(r, g, b) - min(r, g, b) <= 55:
                bright += 1
            tot += 1
    print(f"{f}: hull-band bright = {100.0 * bright / max(tot, 1):5.2f}%  ({bright} px)")
