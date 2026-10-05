#!/usr/bin/env python3
"""Headless render-critique tool for ElectroBench screenshots.

Loads PPM/PNG frames, computes scene-health statistics (exposure clipping,
colour cast, contrast, overbright region geometry) and prints a terse,
actionable critique. Untracked local tooling -- do NOT commit.

Usage: python3 scripts/render_critique.py frame1.ppm [frame2.png ...]
"""
import sys
import os
from collections import deque

import numpy as np
from PIL import Image


def load_frame(path):
    """Return HxWx3 float RGB in [0,1]. Parses PPM headers properly."""
    if path.lower().endswith(".ppm"):
        with open(path, "rb") as f:
            data = f.read()
        # tokenize header: P6, width, height, maxval, then single whitespace
        tokens = []
        i = 0
        while len(tokens) < 4:
            while i < len(data) and data[i : i + 1].isspace():
                i += 1
            if data[i : i + 1] == b"#":
                while i < len(data) and data[i] != 10:
                    i += 1
                continue
            j = i
            while j < len(data) and not data[j : j + 1].isspace():
                j += 1
            tokens.append(data[i:j])
            i = j
        i += 1  # the single whitespace after maxval
        w, h = int(tokens[1]), int(tokens[2])
        px = np.frombuffer(data, dtype=np.uint8, offset=i).reshape(h, w, 3)
        return px.astype(np.float32) / 255.0
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float32) / 255.0


def luma(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def largest_blob(mask):
    """Largest 4-connected component of a bool mask: (area_px, bbox)."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    best = (0, None)
    for sy, sx in zip(*np.nonzero(mask)):
        if seen[sy, sx]:
            continue
        q = deque([(sy, sx)])
        seen[sy, sx] = True
        area = 0
        x0 = x1 = sx
        y0 = y1 = sy
        while q:
            y, x = q.popleft()
            area += 1
            x0, x1 = min(x0, x), max(x1, x)
            y0, y1 = min(y0, y), max(y1, y)
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        if area > best[0]:
            best = (area, (x0, y0, x1, y1))
    return best


def critique(path):
    rgb = load_frame(path)
    h, w, _ = rgb.shape
    L = luma(rgb)
    sat = rgb.max(axis=-1) - rgb.min(axis=-1)
    name = os.path.basename(path)

    print(f"== {name}  ({w}x{h}) ==")
    print(
        f"  luma   mean={L.mean():.3f}  p50={np.percentile(L,50):.3f}  "
        f"p95={np.percentile(L,95):.3f}  p99={np.percentile(L,99):.3f}  max={L.max():.3f}"
    )
    print(
        f"  clip   over(>0.97)={100*(L>0.97).mean():.2f}%  "
        f"under(<0.03)={100*(L<0.03).mean():.2f}%"
    )
    print(
        f"  colour R/G/B mean={rgb[...,0].mean():.3f}/{rgb[...,1].mean():.3f}/"
        f"{rgb[...,2].mean():.3f}  sat mean={sat.mean():.3f}"
    )
    # region breakdown (thirds vertically)
    for label, sl in (("top", slice(0, h // 3)),
                      ("mid", slice(h // 3, 2 * h // 3)),
                      ("bot", slice(2 * h // 3, h))):
        r = rgb[sl]
        print(
            f"  region {label:3s} luma={L[sl].mean():.3f}  "
            f"over={100*(L[sl]>0.97).mean():.2f}%  "
            f"RGB={r[...,0].mean():.2f}/{r[...,1].mean():.2f}/{r[...,2].mean():.2f}"
        )
    # overbright blob geometry: the "white dome / starburst" detector
    mask = L > 0.93
    if mask.any():
        area, bbox = largest_blob(mask)
        pct = 100.0 * area / (h * w)
        x0, y0, x1, y1 = bbox
        bw, bh = x1 - x0 + 1, y1 - y0 + 1
        fill = area / (bw * bh)
        print(
            f"  BLOB>0.93: {pct:.2f}% of frame  bbox=({x0},{y0})-({x1},{y1}) "
            f"{bw}x{bh}  fill={fill:.2f}"
            + ("   <-- SOLID MASS (dome/sheet!)" if fill > 0.55 and bw > w * 0.08 else "")
        )
    else:
        print("  BLOB>0.93: none")
    # contrast: rms of laplacian as a crispness proxy
    gy, gx = np.gradient(L)
    print(f"  detail grad-rms={np.sqrt((gx**2+gy**2).mean()):.4f}")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        critique(p)
