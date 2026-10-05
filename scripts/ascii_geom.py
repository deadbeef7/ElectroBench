#!/usr/bin/env python3
"""ASCII dump of the flat-sky geometry mask, max-pooled so 1 px wires survive.

chafa renders a large uniform area as blank space, which makes it impossible
to tell "empty" from "not printed". This prints one character per cell and
uses OR (max) pooling, so a single geometry pixel in a cell always shows up.

  ascii_geom.py in.ppm bg_r bg_g bg_b [cols] [rows]
"""
import sys
import numpy as np
from PIL import Image


def main(inp, br, bg, bb, cols=118, rows=52):
    cols, rows = int(cols), int(rows)
    a = np.asarray(Image.open(inp).convert("RGB")).astype(np.int16)
    d = np.abs(a - np.array([int(br), int(bg), int(bb)], np.int16)).sum(axis=2)
    mask = d > 26
    h, w = mask.shape
    # integer cell bins, then OR-pool
    yb = (np.arange(h) * rows // h)
    xb = (np.arange(w) * cols // w)
    flat = (yb[:, None] * cols + xb[None, :]).ravel()
    cell = np.zeros(rows * cols, np.int32)
    np.maximum.at(cell, flat, mask.astype(np.int32).ravel())
    cell = cell.reshape(rows, cols)
    print(f"{inp}  {w}x{h} -> {cols}x{rows} cells, "
          f"geometry {100.0*mask.mean():.1f}%")
    print("+" + "-" * cols + "+")
    for r in range(rows):
        line = "".join("#" if cell[r, c] else "." for c in range(cols))
        print("|" + line + "|")
    print("+" + "-" * cols + "+")


if __name__ == "__main__":
    main(*sys.argv[1:])
