#!/usr/bin/env python3
"""Edge map of a render, for seeing thin geometry against a bright sky.

Local-median background subtraction absorbs exactly the objects we care about
(a 6 px pole is thinner than the median window, so the median just absorbs it
and reports "no difference"). A gradient magnitude does not: it fires on the
wires, coils, mast bracing and pole edges no matter how thin they are, and it
ignores the smooth sky. Good for answering "what is that shape".

  edges.py in.png out.png [x0 y0 x1 y1 [scale]]
"""
import sys
import numpy as np
from PIL import Image


def main(inp, outp, *rest):
    box = tuple(int(v) for v in rest[:4]) if len(rest) >= 4 else None
    scale = int(rest[4]) if len(rest) >= 5 else 3
    a = np.asarray(Image.open(inp).convert("RGB")).astype(np.float32)
    lum = a @ np.array([0.299, 0.587, 0.114], np.float32)
    gx = np.zeros_like(lum)
    gy = np.zeros_like(lum)
    gx[:, 1:-1] = lum[:, 2:] - lum[:, :-2]
    gy[1:-1, :] = lum[2:, :] - lum[:-2, :]
    mag = np.sqrt(gx * gx + gy * gy)
    # robust scale: the 99.2nd percentile is a real edge, the tail is noise
    hi = max(float(np.percentile(mag, 99.2)), 12.0)
    v = np.clip(mag / hi, 0.0, 1.0)
    v = v * v * (3.0 - 2.0 * v)          # smoothstep, so faint edges read
    img = (255.0 * (1.0 - v)).astype(np.uint8)
    im = Image.fromarray(np.stack([img] * 3, axis=2))
    if box:
        im = im.crop(box)
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(outp)
    print(f"{outp}  {im.width}x{im.height}  edge scale hi={hi:.1f}")


if __name__ == "__main__":
    main(*sys.argv[1:])
