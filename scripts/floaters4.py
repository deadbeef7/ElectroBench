#!/usr/bin/env python3
"""Final cylinder sweep: bridge small mask gaps, then ask what is still detached.

The earlier scans disagree because the mask severs sub-pixel connections. This
dilates the strict mask by `r` pixels before labelling: a shaft that reaches the
ground through a 2 px gap merges with the ground component and stops reading as
a free bar. Whatever is STILL a tall narrow blob clear of the horizon after
bridging is a genuine floater.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import label  # noqa: E402

HORIZON_FRAC = 0.556


def object_mask(a):
    r, g, b = (a[:, :, i].astype(np.float32) for i in range(3))
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    mx = a.max(axis=2).astype(np.float32)
    mn = a.min(axis=2).astype(np.float32)
    sat = np.where(mx > 1.0, (mx - mn) / np.maximum(mx, 1.0), 0.0)
    return (lum < 165.0) | ((lum < 205.0) & (sat < 0.42))


def main(path, rad):
    a = np.asarray(Image.open(path).convert("RGB"))
    h, w = a.shape[:2]
    hy = int(h * HORIZON_FRAC)
    m = object_mask(a)
    m[:hy, :] &= True
    m[hy:, :] = False
    m[:2, :] = False
    img = Image.fromarray((m * 255).astype(np.uint8))
    img = img.filter(ImageFilter.MaxFilter(3))
    if rad > 1:
        for _ in range(rad - 1):
            img = img.filter(ImageFilter.MaxFilter(3))
    md = np.asarray(img) > 127
    lab = label(md)
    sizes = np.bincount(lab.ravel())
    bars = []
    for i in range(1, len(sizes)):
        if sizes[i] < 25:
            continue
        ys, xs = np.nonzero(lab == i)
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        ch, cw = y1 - y0 + 1, x1 - x0 + 1
        if ch >= 12 and ch > 2 * cw and y0 > 30 and hy - y1 > 12:
            bars.append((sizes[i], x0, x1, y0, y1, cw, ch))
    bars.sort(reverse=True)
    print(f"{path} dilate={rad} horizon={hy}  free bars = {len(bars)}")
    for n, x0, x1, y0, y1, cw, ch in bars[:14]:
        print(f"   n={n:6d} x {x0:4d}..{x1:4d} y {y0:4d}..{y1:4d} w={cw:3d} h={ch:3d}"
              f" gap={hy - y1:4d}")


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3)
