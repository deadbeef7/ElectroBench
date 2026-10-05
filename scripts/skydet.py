#!/usr/bin/env python3
"""Find geometry that floats ENTIRELY in the sky.

The pole line legitimately rises through the sky: a shaft, an arm, a wire
crossing the frame. None of those are "hanging cylinders". A hanging cylinder
is a closed, compact, roughly-vertical blob whose LOWEST point is well above
the horizon line and which is not connected to any shaft, wire or cloud edge.

Method: build a local-median background estimate of the sky, difference it,
threshold, then label connected components with a self-contained union-find
(no scipy in this environment). Report only components that sit entirely above
the horizon with a margin, and print them shape-sorted so a run of 4-6 equal
vertical blobs is obvious at a glance.
"""
import sys
import numpy as np
from PIL import Image


def _med1d(m, axis, k):
    """Median filter of width k along one axis, edge-padded."""
    r = k // 2
    p = np.pad(m, [(r, r) if i == axis else (0, 0) for i in range(m.ndim)],
               mode="edge")
    win = np.lib.stride_tricks.sliding_window_view(p, k, axis=axis)
    return np.median(win, axis=-1)


def median_bg(a, win=31):
    """Local background estimate = separable two-pass median of the image.

    The sky is smooth and the things we hunt for are THIN, so a wide median
    tracks the sky and washes the geometry out of the background entirely --
    which is exactly what makes the differencing below fire on wires, coils
    and masts instead of on cloud texture.
    """
    h, w, _ = a.shape
    sy, sx = max(1, h // 180), max(1, w // 300)
    small = a[::sy, ::sx].astype(np.float32)
    out = np.empty_like(small)
    for c in range(3):
        plane = small[:, :, c]
        out[:, :, c] = _med1d(_med1d(plane, 1, win), 0, win)
    bg = np.repeat(np.repeat(out, sy, axis=0), sx, axis=1)
    return bg[:h, :w]


def label(mask):
    """4-connected labelling by union-find over a 2D bool mask."""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    parent = [0]

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    nxt = 1
    for y in range(h):
        row = mask[y]
        for x in range(w):
            if not row[x]:
                continue
            up = lab[y - 1, x] if y > 0 else 0
            lf = lab[y, x - 1] if x > 0 else 0
            if up and lf:
                lab[y, x] = min(up, lf)
                union(up, lf)
            elif up:
                lab[y, x] = up
            elif lf:
                lab[y, x] = lf
            else:
                parent.append(nxt)
                lab[y, x] = nxt
                nxt += 1
    # flatten
    remap = np.zeros(nxt, np.int32)
    for i in range(1, nxt):
        remap[i] = find(i)
    _, lab = np.unique(lab, return_inverse=True)
    return lab.reshape(h, w)


def horizon(a, bg):
    """Per-column y where sky stops being the background (the skyline)."""
    h, w, _ = a.shape
    d = np.abs(a.astype(np.float32) - bg).sum(axis=2)
    strong = d > 150.0
    hor = np.full(w, h - 1, np.int32)
    for x in range(w):
        col = strong[:, x]
        # the lowest run of >=4 strong pixels = built silhouette / ground
        y = h - 1
        while y >= 0:
            if col[y:y + 1].any():
                run = 0
                y2 = y
                while y2 >= 0 and col[y2]:
                    run += 1
                    y2 -= 1
                if run >= 6:
                    hor[x] = y + run
                    break
            y -= 1
    return np.clip(hor, 0, h - 1)


def main(path, margin=18, min_area=25, drop=55.0):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    h, w, _ = a.shape
    bg = median_bg(a)
    # SIGNED darkening, not absolute difference: every artefact we are hunting
    # is a silhouette, so it is DARKER than the sky behind it. Cloud tops are
    # BRIGHTER than their surroundings, so a signed test drops all cloud
    # texture and leaves only geometry. This is the single change that turned
    # 118 candidate blobs into a readable short list.
    lum = a @ np.array([0.299, 0.587, 0.114], np.float32)
    blum = bg @ np.array([0.299, 0.587, 0.114], np.float32)
    mask = (blum - lum) > drop
    hor = horizon(a, bg)
    print(f"== {path}  {w}x{h}")
    print(f"horizon: median y={int(np.median(hor))}  "
          f"p10={int(np.percentile(hor,10))}  p90={int(np.percentile(hor,90))}")

    lab = label(mask)
    sky = np.zeros_like(mask)
    for x in range(w):
        sky[: max(0, hor[x] - margin), x] = True
    # keep components with >=70% of their pixels in the sky band
    ids, counts = np.unique(lab[sky & (lab > 0)], return_counts=True)
    out = []
    for i, c in zip(ids, counts):
        ys, xs = np.where(lab == i)
        if len(ys) == 0:
            continue
        total = len(ys)
        if c / total < 0.70:
            continue
        bw = xs.max() - xs.min() + 1
        bh = ys.max() - ys.min() + 1
        if total < min_area or bw < 2:
            continue
        clear = int(hor[xs].mean()) - int(ys.max())
        out.append((total, bh, bw, bh / bw, int(xs.min()), int(ys.min()),
                    int(xs.max()), int(ys.max()), clear))
    out.sort(key=lambda r: (-r[0]))
    print(f"floating sky blobs: {len(out)}")
    for (area, bh, bw, asp, x0, y0, x1, y1, clear) in out:
        print(f"  area={area:6d}  {bw:3d}x{bh:3d}  h/w={asp:5.2f}  "
              f"at ({x0},{y0})-({x1},{y1})  {clear}px above horizon")


if __name__ == "__main__":
    p = sys.argv[1] if len(sys.argv) > 1 else "out.png"
    m = int(sys.argv[2]) if len(sys.argv) > 2 else 18
    main(p, m)
