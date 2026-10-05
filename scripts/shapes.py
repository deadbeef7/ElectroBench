#!/usr/bin/env python3
"""Find detached GEOMETRY in a render: compact blobs whose colour differs from
a locally-smoothed background, reported with an ASPECT RATIO so a vertical
cylinder can be told apart from a round cloud puff.

Written because "floating cylinders" is a SHAPE complaint. The existing
floaters.py only tested colour against a single median sky sample, and a bright
warm object against a bright warm dusk sky passes that test while being
obviously wrong to the eye. This one differences every pixel against a local
median (the sky is smooth, geometry is not), so it does not care what colour
the sky is today.

No scipy here, so the box median is a strided sample rather than a true median
-- good enough to remove the smooth gradient, cheap enough for 1600x900.

Usage: python3 scripts/shapes.py frame.png [max_area]
"""
import sys
from collections import deque

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def box_median(img, k):
    """Strided box median over a k x k window (approximate, but the sky's
    gradient is slow enough that a strided sample removes it cleanly)."""
    h, w, c = img.shape
    pad = k // 2
    padded = np.pad(img, ((pad, pad), (pad, pad), (0, 0)), mode="edge")
    out = np.empty_like(img)
    step = max(1, k // 5)
    for y in range(h):
        ys = slice(y, y + k, step)
        for x in range(w):
            xs = slice(x, x + k, step)
            out[y, x] = np.median(padded[ys, xs], axis=(0, 1))
    return out


def main():
    path = sys.argv[1]
    max_area = int(sys.argv[2]) if len(sys.argv) > 2 else 260
    img = load(path)
    h, w, _ = img.shape
    med = box_median(img, 41)
    diff = np.abs(img - med).sum(axis=2)
    mask = diff > 78                      # "this pixel is not the smooth sky"

    seen = np.zeros((h, w), bool)
    blobs = []
    for y in range(h):
        row = mask[y]
        for x in np.nonzero(row & ~seen[y])[0]:
            if seen[y, x]:
                continue
            q = deque([(y, x)])
            seen[y, x] = True
            pts = []
            while q:
                cy, cx = q.popleft()
                pts.append((cy, cx))
                if len(pts) > 4000:
                    break
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] \
                                and not seen[ny, nx]:
                            seen[ny, nx] = True
                            q.append((ny, nx))
            if len(pts) >= 9:
                ys = [p[0] for p in pts]
                xs = [p[1] for p in pts]
                blobs.append((len(pts), min(xs), min(ys),
                              max(xs) - min(xs) + 1, max(ys) - min(ys) + 1,
                              pts))

    blobs.sort(key=lambda b: -b[0])
    print("== %s  %dx%d ==" % (path, w, h))
    print("detached-from-background blobs (area <= %d), tallest first:"
          % max_area)
    rows = []
    for area, x0, y0, bw, bh, pts in blobs:
        if area > max_area or bh < 6 or bw < 3:
            continue
        if area > 0.45 * bw * bh:      # solid blocks are buildings, not cables
            continue
        rows.append((area, x0, y0, bw, bh))
    rows.sort(key=lambda r: -(r[4] / max(r[3], 1)))
    shown = 0
    for area, x0, y0, bw, bh in rows:
        cy = sum(p[0] for p in [])  # placeholder
        print("   area=%4d  %2dx%-2d  aspect h/w=%.2f  at (%d,%d)-(%d,%d)"
              % (area, bw, bh, bh / float(bw), x0, y0, x0 + bw, y0 + bh))
        shown += 1
        if shown >= 30:
            break
    if shown == 0:
        print("   none")


main()