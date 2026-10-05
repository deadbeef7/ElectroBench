#!/usr/bin/env python3
"""Relaxed-mask version of the floater scan.

floaters2.py uses a strict darkness+saturation mask, which is good at finding
*detached* things but severs sub-pixel connections: a 2 px pole shaft or a
guy wire drops out of the mask and its crown becomes a "free bar". This reruns
the same tall-narrow-blob query with a mask that accepts any pixel measurably
darker than its own row, then reports whether each bar survives as a separate
component or merges into ground-attached geometry.
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import label  # noqa: E402

HORIZON_FRAC = 0.556


def main(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    h, w = a.shape[:2]
    hy = int(h * HORIZON_FRAC)
    rowmed = np.median(a, axis=1, keepdims=True)
    delta = (rowmed - a).sum(axis=2)
    m = delta > 8.0
    m[hy:, :] = False
    m[:2, :] = False
    lab = label(m)
    sizes = np.bincount(lab.ravel())
    print(f"{path} {w}x{h} horizon={hy} relaxed components={len(sizes) - 1}")
    rows = []
    for i in range(1, len(sizes)):
        if sizes[i] < 25:
            continue
        ys, xs = np.nonzero(lab == i)
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        ch, cw = y1 - y0 + 1, x1 - x0 + 1
        if ch >= 12 and ch > 2 * cw and y0 > 30 and hy - y1 > 12:
            rows.append((sizes[i], x0, x1, y0, y1, cw, ch))
    rows.sort(reverse=True)
    print(f"  tall narrow bars: {len(rows)}")
    for n, x0, x1, y0, y1, cw, ch in rows:
        print(f"  n={n:6d}  x {x0:4d}..{x1:4d}  y {y0:4d}..{y1:4d}  w={cw:3d} h={ch:3d}"
              f"  gap={hy - y1:4d}")


if __name__ == "__main__":
    main(sys.argv[1])
