#!/usr/bin/env python3
"""Locate solid geometry silhouetted against the sky in a dusk render.

The sky here is a bright orange gradient with brighter cloud decks, so
"geometry" == a pixel DARKER than the smooth local sky around it (poles, wires,
masts, radomes are all dark; clouds are brighter). We difference against a
strided box median, then keep only components that sit in the sky band, and
report bbox + aspect so a vertical cylinder is distinguishable from a puff.
"""
import sys
from collections import deque
import numpy as np
from PIL import Image


def box_median(img, k):
    h, w, _ = img.shape
    pad = k // 2
    p = np.pad(img, ((pad, pad), (pad, pad), (0, 0)), mode="edge")
    step = max(1, k // 5)
    # rows/cols subsample: build with reshape-style striding
    out = np.empty_like(img)
    ys_all = np.arange(h)[:, None] + np.arange(0, k, step)[None, :]   # h x n
    xs_all = np.arange(w)[:, None] + np.arange(0, k, step)[None, :]
    for y in range(h):
        ys = ys_all[y]
        for x in range(w):
            xs = xs_all[x]
            out[y, x] = np.median(p[ys][:, xs], axis=(0, 1))
    return out


def main():
    for path in sys.argv[1:]:
        img = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
        h, w, _ = img.shape
        med = box_median(img, 45)
        diff = (med - img).sum(axis=2)      # positive => darker than local sky
        mask = diff > 90
        # sky band = above the first row that is mostly ground-coloured
        seen = np.zeros((h, w), bool)
        comps = []
        for y in range(h):
            for x in np.nonzero(mask[y] & ~seen[y])[0]:
                if seen[y, x]:
                    continue
                q = deque([(y, x)])
                seen[y, x] = True
                pts = []
                while q:
                    cy, cx = q.popleft()
                    pts.append((cy, cx))
                    if len(pts) > 20000:
                        break
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] \
                                    and not seen[ny, nx]:
                                seen[ny, nx] = True
                                q.append((nx, ny)) if False else q.append((ny, nx))
                ys = [p[0] for p in pts]
                xs = [p[1] for p in pts]
                comps.append((len(pts), min(xs), min(ys),
                              max(xs) - min(xs) + 1, max(ys) - min(ys) + 1,
                              pts))
        comps.sort(key=lambda c: -c[0])
        print(f"== {path} {w}x{h}  {len(comps)} dark components ==")
        shown = 0
        for area, x0, y0, bw, bh, pts in comps:
            if area < 14:
                continue
            if area > 0.5 * bw * bh and bh < 12:
                continue
            # sky band only: ignore things whose top is well below frame centre
            # AND that touch the bottom (ground-rooted)
            if y0 > h * 0.62:
                continue
            print(f"   area={area:6d} {bw:3d}x{bh:3d} h/w={bh/max(bw,1):5.2f} "
                  f"({x0},{y0})-({x0+bw},{y0+bh})")
            shown += 1
            if shown >= 26:
                break
        if not shown:
            print("   none")


main()
