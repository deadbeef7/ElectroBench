#!/usr/bin/env python3
"""Find foreground components in scene-4 frames that are detached from the ground.

A free-floating object (a "cylinder in the air") shows up as a connected blob of
non-sky pixels whose bounding box never touches the treeline / horizon band.
"""
import sys
import numpy as np
from PIL import Image

HORIZON_FRAC = 0.556  # y=238 of 428 in the 760x428 shots


def object_mask(a: np.ndarray) -> np.ndarray:
    r = a[:, :, 0].astype(np.float32)
    g = a[:, :, 1].astype(np.float32)
    b = a[:, :, 2].astype(np.float32)
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    mx = a.max(axis=2).astype(np.float32)
    mn = a.min(axis=2).astype(np.float32)
    sat = np.where(mx > 1.0, (mx - mn) / np.maximum(mx, 1.0), 0.0)
    # sky is bright and orange; objects are darker and less saturated
    return (lum < 165.0) | ((lum < 205.0) & (sat < 0.42))


def components(mask: np.ndarray):
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    out = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or seen[y, x]:
                continue
            stack = [(y, x)]
            seen[y, x] = True
            pix = []
            while stack:
                cy, cx = stack.pop()
                pix.append((cy, cx))
                for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            ys = [p[0] for p in pix]
            xs = [p[1] for p in pix]
            out.append({
                "n": len(pix),
                "x0": min(xs), "x1": max(xs),
                "y0": min(ys), "y1": max(ys),
            })
    return out


def main(paths, bars_only=False):
    for p in paths:
        a = np.asarray(Image.open(p).convert("RGB"))
        h, w = a.shape[:2]
        hy = int(h * HORIZON_FRAC)
        m = object_mask(a)
        m[hy:, :] = False  # ignore everything at/below the treeline
        m[:2, :] = False   # ignore the HUD text band
        comps = [c for c in components(m) if c["n"] >= 25]
        if bars_only:
            # tall narrow blobs well clear of the ground = the "cylinder" signature
            bars = [c for c in comps
                    if c["y0"] > 30
                    and (c["y1"] - c["y0"] + 1) >= 12
                    and (c["y1"] - c["y0"] + 1) > 2 * (c["x1"] - c["x0"] + 1)
                    and hy - c["y1"] > 12]
            print(f"\n== {p} BARS {len(bars)} of {len(comps)}")
            for c in sorted(bars, key=lambda c: -c["n"]):
                wdt = c["x1"] - c["x0"] + 1
                hgt = c["y1"] - c["y0"] + 1
                print(f"  n={c['n']:6d}  x {c['x0']:4d}..{c['x1']:4d}  y {c['y0']:4d}..{c['y1']:4d}"
                      f"  w={wdt:3d} h={hgt:3d}  gap={hy - c['y1']:4d}")
            continue
        comps.sort(key=lambda c: -c["n"])
        print(f"\n== {p}  ({w}x{h}) horizon y={hy}  components={len(comps)}")
        for c in comps[:25]:
            wdt = c["x1"] - c["x0"] + 1
            hgt = c["y1"] - c["y0"] + 1
            print(f"  n={c['n']:6d}  x {c['x0']:4d}..{c['x1']:4d}  y {c['y0']:4d}..{c['y1']:4d}"
                  f"  w={wdt:3d} h={hgt:3d}  gap_above_horizon={hy - c['y1']:4d}")


if __name__ == "__main__":
    args = sys.argv[1:]
    bars = False
    if args and args[0] == "--bars":
        bars = True
        args = args[1:]
    main(args or ["/tmp/sweep/f%d.png" % i for i in range(4)], bars)
