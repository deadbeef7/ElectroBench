#!/usr/bin/env python3
"""Geometry-only view for the flat-sky debug render.

With the sky flattened to a constant colour, EVERY pixel that differs from
that colour is geometry. This turns "hanging cylinders in the sky" from a
squint into a measurement: it reports each connected silhouette, whether its
lowest point is above the horizon line, and its aspect ratio.

  geommap.py in.ppm out.png [bg_r bg_g bg_b]
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import label  # noqa: E402


def main(inp, outp, *rest):
    bg = tuple(int(v) for v in rest[:3]) if len(rest) >= 3 else (107, 107, 107)
    a = np.asarray(Image.open(inp).convert("RGB")).astype(np.int16)
    d = np.abs(a - np.array(bg, np.int16)).sum(axis=2)
    mask = d > 26
    img = np.where(mask, 20, 245).astype(np.uint8)
    Image.fromarray(np.stack([img] * 3, axis=2)).save(outp)

    h, w = mask.shape
    lab = label(mask)
    # horizon: the topmost full-width row that is mostly geometry
    rows = mask.mean(axis=1)
    ground = np.where(rows > 0.35)[0]
    hor = int(ground[0]) if len(ground) else h - 1

    print(f"{outp}  {w}x{h}  geometry {100.0*mask.mean():.2f}%  "
          f"horizon y={hor}")
    ids, cnt = np.unique(lab[mask], return_counts=True)
    out = []
    for i, c in zip(ids, cnt):
        ys, xs = np.where(lab == i)
        bw = int(xs.max() - xs.min() + 1)
        bh = int(ys.max() - ys.min() + 1)
        bottom = int(ys.max())
        out.append((int(c), bw, bh, bh / bw, int(xs.min()), int(ys.min()),
                    int(xs.max()), bottom, hor - bottom))
    # floating first: things whose bottom is well clear of the horizon
    out.sort(key=lambda r: (-r[8], -r[0]))
    print("FLOATING (bottom above horizon by >8px), largest first:")
    n = 0
    for (c, bw, bh, asp, x0, y0, x1, y1, clr) in out:
        if clr <= 8 or c < 12:
            continue
        n += 1
        if n > 28:
            break
        print(f"  area={c:6d} {bw:3d}x{bh:3d} h/w={asp:5.2f} "
              f"at ({x0},{y0})-({x1},{y1})  {clr}px clear")
    if not n:
        print("  (none)")
    print("GROUND-ANCHORED (largest 10):")
    for (c, bw, bh, asp, x0, y0, x1, y1, clr) in sorted(out, key=lambda r: -r[0])[:10]:
        print(f"  area={c:6d} {bw:3d}x{bh:3d} h/w={asp:5.2f} "
              f"at ({x0},{y0})-({x1},{y1})  {clr:+d}px")


if __name__ == "__main__":
    main(*sys.argv[1:])
