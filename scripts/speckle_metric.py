#!/usr/bin/env python3
"""Speckle analyzer for ElectroBench tide screenshots.

Counts salt-and-pepper speckles: bright pixels whose entire local
neighborhood is far darker (isolated high-frequency dots), plus a coarse
ASCII luminance map for orientation. Usage:

  python3 scripts/speckle_metric.py shot.ppm [shot2.ppm ...]

Accepts binary P6 PPMs directly, or 8-bit PNGs converted beforehand.
"""
import sys


def read_image(path):
    """Return (width, height, lum[y][x]) with lum in 0..1."""
    data = open(path, "rb").read()
    if data[:2] == b"P6":
        # parse PPM header: magic, w, h, maxval (whitespace/comment separated)
        fields, i = [], 2
        while len(fields) < 3:
            while i < len(data) and data[i : i + 1].isspace():
                i += 1
            if data[i : i + 1] == b"#":
                while data[i : i + 1] not in (b"\n", b""):
                    i += 1
                continue
            j = i
            while j < len(data) and not data[j : j + 1].isspace():
                j += 1
            fields.append(int(data[i:j]))
            i = j
        w, h, maxval = fields
        i += 1  # single whitespace after maxval
        px = data[i : i + w * h * 3]
        stride, lum = w * 3, []
        for y in range(h):
            row = px[y * stride : (y + 1) * stride]
            lum.append(
                [
                    (0.2126 * row[x * 3] + 0.7152 * row[x * 3 + 1] + 0.0722 * row[x * 3 + 2])
                    / maxval
                    for x in range(w)
                ]
            )
        return w, h, lum
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: PNG not supported directly, convert with: convert {path} -depth 8 out.ppm")
    raise SystemExit(f"{path}: not a P6 PPM")


def speckles(w, h, lum, bright=0.55, contrast=0.45):
    """Pixels >= bright whose 8 neighbours are all < contrast * value."""
    hits = []
    for y in range(1, h - 1):
        row = lum[y]
        for x in range(1, w - 1):
            v = row[x]
            if v < bright:
                continue
            lim = v * contrast
            if (
                lum[y - 1][x - 1] < lim and lum[y - 1][x] < lim and lum[y - 1][x + 1] < lim
                and row[x - 1] < lim and row[x + 1] < lim
                and lum[y + 1][x - 1] < lim and lum[y + 1][x] < lim and lum[y + 1][x + 1] < lim
            ):
                hits.append((x, y, v))
    return hits


def band_stats(w, h, lum, y0, y1, label):
    """Cluster speckles into blobs and summarise a horizontal band."""
    hits = speckles(w, h, lum)
    in_band = [(x, y, v) for (x, y, v) in hits if y0 <= y < y1]
    # union-find-ish clustering on 8-neighbour adjacency
    blobs, seen = [], set()
    hset = {(x, y) for (x, y, _) in in_band}
    for p in hset:
        if p in seen:
            continue
        stack, blob = [p], []
        seen.add(p)
        while stack:
            cx, cy = stack.pop()
            blob.append((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (cx + dx, cy + dy)
                    if q in hset and q not in seen:
                        seen.add(q)
                        stack.append(q)
        blobs.append(blob)
    blobs.sort(key=len, reverse=True)
    print(f"  {label}: speckle px={len(in_band)} blobs={len(blobs)}"
          f" biggest={len(blobs[0]) if blobs else 0}px")
    for b in blobs[:5]:
        xs = [p[0] for p in b]
        ys = [p[1] for p in b]
        cx, cy = sum(xs) // len(b), sum(ys) // len(b)
        print(f"    blob {len(b)}px at ({cx},{cy}) bbox {max(xs)-min(xs)+1}x{max(ys)-min(ys)+1}")
    return hits


def ascii_map(w, h, lum, cols=48):
    rows = 16
    cw, ch = w / cols, h / rows
    chars = " .:-=+*#%@"
    print("  luminance map:")
    for r in range(rows):
        line = ""
        for c in range(cols):
            x0, y0 = int(c * cw), int(r * ch)
            block = [
                lum[y][x]
                for y in range(y0, min(h, int((r + 1) * ch)))
                for x in range(x0, min(w, int((c + 1) * cw)))
            ]
            m = sum(block) / len(block)
            line += chars[min(9, int(m * 10))]
        print(f"    {line}")


for path in sys.argv[1:]:
    w, h, lum = read_image(path)
    mean = sum(sum(r) for r in lum) / (w * h)
    horizon = int(h * 0.52)
    print(f"{path}: {w}x{h} mean={mean:.4f}")
    band_stats(w, h, lum, horizon, h, "whole sea")
    band_stats(w, h, lum, int(h * 0.60), int(h * 0.80), "mid-sea band (circled zone)")
    ascii_map(w, h, lum)
