#!/usr/bin/env python3
"""Threshold-free cylinder check on a flat-sky debug render.

With the sky shader forced to a constant colour, every pixel that is NOT that
colour is geometry -- no darkness threshold, no local-median background, no
tuning. Then flood-fill the geometry mask seeded from the BOTTOM EDGE: anything
that fill reaches is anchored to the ground, and anything it does not reach is
detached. A hanging cylinder must appear in the unanchored set.

  anchored.py in.ppm [bg_r bg_g bg_b]
"""
import sys
from collections import deque
import numpy as np
from PIL import Image

MIN_AREA = 12


def main(path, *rest):
    bg = np.array([int(v) for v in rest[:3]], np.int16) if len(rest) >= 3 \
        else np.array([107, 117, 127], np.int16)
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    h, w = a.shape[:2]
    geom = (np.abs(a - bg).sum(axis=2) > 18)
    geom[:2, :] = False            # ignore any HUD text on the top edge
    seen = np.zeros_like(geom)
    dq = deque()
    for x in range(w):
        for y in (h - 1, h - 2):
            if geom[y, x] and not seen[y, x]:
                seen[y, x] = True
                dq.append((y, x))
    while dq:                       # 8-connected so a 1 px gap is not a bridge
        y, x = dq.popleft()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and geom[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    dq.append((ny, nx))
    free = geom & ~seen
    print(f"{path} {w}x{h} bg={tuple(int(v) for v in bg)}  geometry={geom.sum()}  "
          f"anchored={seen.sum()}  DETACHED={free.sum()}")

    lab = -np.ones((h, w), np.int32)
    comps = []
    for y0 in range(h):
        for x0 in range(w):
            if not free[y0, x0] or lab[y0, x0] >= 0:
                continue
            i = len(comps)
            lab[y0, x0] = i
            q = deque([(y0, x0)])
            pix = []
            while q:
                y, x = q.popleft()
                pix.append((y, x))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < h and 0 <= nx < w and free[ny, nx] and lab[ny, nx] < 0:
                            lab[ny, nx] = i
                            q.append((ny, nx))
            ys = [p[0] for p in pix]
            xs = [p[1] for p in pix]
            comps.append((len(pix), min(xs), max(xs), min(ys), max(ys)))
    comps.sort(reverse=True)
    big = [c for c in comps if c[0] >= MIN_AREA]
    print(f"  detached components (>= {MIN_AREA}px): {len(big)} of {len(comps)}")
    for n, x0, x1, y0, y1 in big[:20]:
        bw, bh = x1 - x0 + 1, y1 - y0 + 1
        print(f"   n={n:6d} x {x0:4d}..{x1:4d} y {y0:4d}..{y1:4d}  {bw:3d}x{bh:3d}"
              f"  h/w={bh / bw:5.2f}  top_edge={'Y' if y0 <= 2 else 'n'}")


if __name__ == "__main__":
    main(sys.argv[1], *sys.argv[2:])
