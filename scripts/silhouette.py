#!/usr/bin/env python3
"""Dump the geometry-only silhouette: everything DARKER than its local sky
background, drawn black on white. Cloud texture and sun glare are brighter
than their surroundings and vanish; wires, coils, masts and poles stay.

This is the view that actually answers "what shape is in the sky", because it
removes every lighting cue that hides a thin object in a noisy render.

  silhouette.py in.png out.png [x0 y0 x1 y1 [scale]]
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import median_bg  # noqa: E402


def main(inp, outp, *rest):
    box = tuple(int(v) for v in rest[:4]) if len(rest) >= 4 else None
    scale = int(rest[4]) if len(rest) >= 5 else 3
    a = np.asarray(Image.open(inp).convert("RGB")).astype(np.float32)
    bg = median_bg(a)
    w3 = np.array([0.299, 0.587, 0.114], np.float32)
    dark = (bg @ w3) - (a @ w3)
    sil = (dark > 55.0)
    img = np.where(sil[..., None], np.float32(16), np.float32(242))
    img = np.repeat(img, 3, axis=2).astype(np.uint8)
    im = Image.fromarray(img)
    if box:
        im = im.crop(tuple(int(v) for v in box))
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(outp)
    print(f"{outp}  {im.width}x{im.height}  "
          f"({int(sil.sum())} silhouette px, {100.0*sil.mean():.2f}%)")


if __name__ == "__main__":
    main(*sys.argv[1:])
