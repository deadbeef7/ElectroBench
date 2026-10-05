#!/usr/bin/env python3
"""Positive control for the cylinder sweep.

The floater queries disagreed with what was seen by eye, so run the SAME
query over the OLD (pre-pylon) GIF and the NEW one at a range of detection
thresholds. A detector that reports zero on the build that visibly had
hanging bars is not a detector; the threshold has to be loose enough that the
old bars light up before the new frames are allowed to pass.
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import median_bg, label, horizon  # noqa: E402

MIN_AREA = 20
MIN_HW = 1.5
MIN_H = 8
MIN_CLEAR = 12
SKY_FRAC = 0.80


def bars(path, drop):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    h, w, _ = a.shape
    bg = median_bg(a)
    k = np.array([0.299, 0.587, 0.114], np.float32)
    mask = ((bg @ k) - (a @ k)) > drop
    hor = horizon(a, bg)
    lab = label(mask)
    sky = np.zeros_like(mask)
    for x in range(w):
        sky[: max(0, int(hor[x]) - 18), x] = True
    ids, counts = np.unique(lab[sky & (lab > 0)], return_counts=True)
    out = []
    for i, c in zip(ids, counts):
        ys, xs = np.where(lab == i)
        total = len(ys)
        if total < MIN_AREA or c / total < SKY_FRAC:
            continue
        bw = xs.max() - xs.min() + 1
        bh = ys.max() - ys.min() + 1
        clear = int(np.median(hor[xs])) - int(ys.max())
        if bh < MIN_H or bh / bw < MIN_HW or clear < MIN_CLEAR:
            continue
        out.append((total, bw, bh, int(xs.min()), int(ys.min()), clear))
    out.sort(reverse=True)
    return out


def main(pairs):
    for drop in (12.0, 20.0, 30.0, 45.0, 60.0):
        print(f"\n### drop = {drop:4.0f} luma")
        for label_, paths in pairs:
            tot = 0
            for p in paths:
                r = bars(p, drop)
                tot += len(r)
                head = "  ".join(f"{a}px {w}x{h}@{x},{y} cl{cl}" for a, w, h, x, y, cl in r[:4])
                print(f"  {label_:<22s} {p.split('/')[-1]:<10s} bars={len(r):3d}  {head}")
            print(f"  {label_:<22s} TOTAL {tot}")


if __name__ == "__main__":
    main([("OLD", [f"/tmp/old/o{i}.png" for i in range(4)]),
          ("NEW", [f"/tmp/sweep/f{i}.png" for i in range(4)])])
