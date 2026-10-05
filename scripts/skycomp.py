#!/usr/bin/env python3
"""List dark (sky-silhouetted) connected components in the sky band with
native-resolution crops for the tall/compact ones."""
import sys
from collections import deque
import numpy as np
from PIL import Image


def med_box(a, k=41):
    h, w, _ = a.shape
    pad = k // 2
    p = np.pad(a, ((pad, pad), (pad, pad), (0, 0)), mode="edge")
    step = max(1, k // 4)
    out = np.empty_like(a)
    ys = np.arange(k)[::step]
    for y in range(h):
        for x in range(w):
            xs = np.arange(x, x + k, step)
            out[y, x] = np.median(p[y + ys][:, xs], axis=(0, 1))
    return out


def main():
    path = sys.argv[1]
    ybot = int(float(sys.argv[2]) * 428) if len(sys.argv) > 2 else 250
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    h, w, _ = a.shape
    med = med_box(a)
    mask = ((med - a).sum(axis=2) > 80)
    mask[ybot:, :] = False
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
                if len(pts) > 30000:
                    break
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] \
                                and not seen[ny, nx]:
                            seen[ny, nx] = True
                            q.append((ny, nx))
            ys = [p[0] for p in pts]
            xs = [p[1] for p in pts]
            comps.append((len(pts), min(xs), min(ys),
                          max(xs) - min(xs) + 1, max(ys) - min(ys) + 1))
    comps.sort(key=lambda c: -(c[4] / max(c[3], 1)))
    print(f"== {path}  sky band y<{ybot}  {len(comps)} components ==")
    for area, x0, y0, bw, bh in comps[:18]:
        asp = bh / max(bw, 1)
        fill = area / (bw * bh)
        print(f"  area={area:6d} {bw:3d}x{bh:3d} h/w={asp:5.2f} fill={fill:4.2f} "
              f"({x0},{y0})-({x0+bw},{y0+bh})")


main()
