#!/usr/bin/env python3
"""Where is the frame actually flat? Row-band profile of local texture energy.

Usage: python3 scripts/p19rows.py frame.ppm [x0 x1 y0 y1]
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, "/home/daytona/codebase/scripts")
from p19probe import load, lum, boxstd  # noqa: E402


def main():
    p = sys.argv[1]
    a = load(p)
    H, W, _ = a.shape
    L = lum(a)
    sd = boxstd(L, 4)

    print(f"== {p} ({W}x{H}) ==")
    print("band y-range   meanL  p50L   medSD  p90SD   %sd<.006")
    for y0 in range(0, H - 39, 40):
        sl = slice(y0, min(H, y0 + 40))
        s = sd[sl]
        s = s[~np.isnan(s)]
        lv = L[sl]
        print(f"  y{y0:4d}-{min(H,y0+40):4d}  {lv.mean():.3f}  {np.percentile(lv,50):.3f}   "
              f"{np.percentile(s,50):.4f}  {np.percentile(s,90):.4f}   "
              f"{100*(s<0.006).mean():5.1f}%")

    # per-column texture in the bottom third, so a road running away from the
    # camera shows up as a band of columns, not a band of rows
    print()
    print("column profile of local texture, bottom third:")
    b = slice(int(H * 0.66), H)
    s = sd[b]
    s = s[~np.isnan(s)]
    prof = []
    for x0 in range(0, W - 39, 40):
        c = sd[b, x0:min(W, x0 + 40)]
        c = c[~np.isnan(c)]
        prof.append(np.percentile(c, 50))
    print("  " + " ".join(f"{v:.3f}" for v in prof))


if __name__ == "__main__":
    main()