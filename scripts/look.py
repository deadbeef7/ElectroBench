"""Write a contrast-stretched, upscaled crop so a thin object can be looked at.

The raw frame puts sky at luma ~172 and every surface at ~92, so an ordinary
crop is a flat wash. This remaps [lo, hi] to [0, 255] over the crop and scales
it up with NEAREST so single pixels survive, then stacks the before/after crops
side by side in one PNG.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def stretch(a, lo, hi):
    n = (a - lo) / max(hi - lo, 1e-6)
    return np.clip(n, 0, 1)


def main():
    out, x0, x1, y0, y1, scale = (sys.argv[1], int(sys.argv[2]), int(sys.argv[3]),
                                  int(sys.argv[4]), int(sys.argv[5]),
                                  int(sys.argv[6]))
    paths = sys.argv[7:]
    crops = []
    for p in paths:
        a = load(p)[y0:y1, x0:x1]
        l = a @ np.array([0.299, 0.587, 0.114])
        lo, hi = np.percentile(l, 1), np.percentile(l, 99)
        n = stretch(a, lo, hi)
        img = Image.fromarray((n * 255).astype(np.uint8))
        img = img.resize(((x1 - x0) * scale, (y1 - y0) * scale), Image.NEAREST)
        crops.append(img)
    W = sum(c.width for c in crops) + 8 * (len(crops) - 1)
    H = max(c.height for c in crops)
    canvas = Image.new("RGB", (W, H), (255, 0, 255))
    cx = 0
    for c in crops:
        canvas.paste(c, (cx, 0))
        cx += c.width + 8
    canvas.save(out)
    print("wrote %s  %dx%d  panels=%d (left=old right=new)"
          % (out, W, H, len(crops)))


main()