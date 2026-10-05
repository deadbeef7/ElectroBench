#!/usr/bin/env python3
"""Per-material pixel census of a BUILD-P18 material+distance tag render.

Answers the two questions P18 has to settle, and the one P16 has to keep
settling:

  1. did the shafts stop standing out?  -> CONCRETE + STEEL pixel counts and
     their distance profile, i.e. how much silhouette the poles now own.
  2. did the wire web start reading?    -> CABLE pixel counts and how those
     pixels are distributed: a web is MANY SMALL components, a regression is
     ONE LARGE BLOB.

The tag block writes R=0 (objects), G=vMat*15, B=whole metres from the eye.
Sky renders through sky_frag.glsl, so any pixel with R >= 2 is sky and can be
used as the local backdrop for any object pixel.

Usage: p18census.py BASE.ppm NEW.ppm [IMG ...]
"""
import sys

import numpy as np

NAMES = ["PAINT", "WOOD", "GROUND", "ROAD", "LINE", "CABLE", "CERAMIC", "METAL",
         "WALL", "ROOF", "GLASS", "LEAF", "SHADOW", "GLOW", "STEEL", "KERB",
         "CONCRETE"]


def load(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def census(path):
    img = load(path)
    obj = img[:, :, 0] < 2
    mat = np.clip(np.rint(img[:, :, 1].astype(np.float64) / 15.0), 0, 16).astype(np.int16)
    dist = img[:, :, 2].astype(np.float64)
    total = img.shape[0] * img.shape[1]
    print("== %s  %dx%d  (%d object px, %.1f%%)"
          % (path, img.shape[1], img.shape[0], obj.sum(), 100.0 * obj.sum() / total))
    for m in (5, 14, 16):
        sel = obj & (mat == m)
        n = int(sel.sum())
        if not n:
            print("   %-9s %6d" % (NAMES[m], 0))
            continue
        d = dist[sel]
        near = int((d < 40).sum())
        mid = int(((d >= 40) & (d < 90)).sum())
        far = int((d >= 90).sum())
        print("   %-9s %6d px   <40m %5d   40-90m %5d   >=90m %5d   mean %.1f m"
              % (NAMES[m], n, near, mid, far, d.mean()))
    # how much sky is left over, which is what P16 was really protecting
    print("   sky       %6d px (%.2f%% of frame)" % (total - int(obj.sum()),
                                                      100.0 * (total - obj.sum()) / total))
    return img, obj, mat, dist


def blobs(path, want=5, minarea=8):
    """Connected components of the CABLE class: a web is many small pieces.

    Pure-python 8-connected flood fill (no scipy in this image): only the
    class's own pixels are visited, so a 30k px class costs well under a
    second.
    """
    from collections import deque

    img = load(path)
    obj = img[:, :, 0] < 2
    mat = np.clip(np.rint(img[:, :, 1].astype(np.float64) / 15.0), 0, 16).astype(np.int16)
    sel = (obj & (mat == 5)).tolist()
    h, w = len(sel), len(sel[0]) if sel else 0
    seen = [[False] * w for _ in range(h)]
    sizes = []
    for y in range(h):
        row = sel[y]
        for x in range(w):
            if not row[x] or seen[y][x]:
                continue
            size = 0
            dq = deque([(y, x)])
            seen[y][x] = True
            while dq:
                cy, cx = dq.popleft()
                size += 1
                for dy in (-1, 0, 1):
                    ny = cy + dy
                    if ny < 0 or ny >= h:
                        continue
                    nrow = sel[ny]
                    for dx in (-1, 0, 1):
                        nx = cx + dx
                        if nx < 0 or nx >= w or seen[ny][nx] or not nrow[nx]:
                            continue
                        seen[ny][nx] = True
                        dq.append((ny, nx))
            sizes.append(size)
    if not sizes:
        print("   CABLE components: 0")
        return
    sizes.sort(reverse=True)
    big = sizes[:want]
    total = sum(sizes)
    print("   CABLE components: %d (>=%d px: %d)   largest: %s   total %d px"
          % (len(sizes), minarea, sum(1 for s in sizes if s >= minarea), big, total))
    print("   top %d share of the CABLE class: %.1f%%"
          % (want, 100.0 * sum(big) / total))


def main():
    paths = sys.argv[1:]
    for p in paths:
        census(p)
        blobs(p)
        print()


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------------------
# The P16 guard, in a metric that survives MSAA.
#
# P16's own measurement (a flat-colour tag pass) read "sky pixels" and "largest
# blob", but that pass records a wire only where a sample lands exactly on the
# palette colour, so a sub-pixel strand arrives as a row of disconnected
# 2-6 px dashes and every blob is small by construction. This pass uses R<2 as
# the object test, which counts antialiased edge pixels too, so a wire arrives
# as one continuous line and whole-span components are 1000+ px. The two
# numbers are not comparable -- but the question P16 was actually asking is:
# does the web still let the sky THROUGH?
#
# So ask that directly. Label the sky. A mesh of separate strands traps many
# small holes between them; a fused wall traps almost none. Sky-hole count and
# total trapped area is the measure, and it is resolution- and MSAA-robust.
def skyholes(path):
    from collections import deque
    img = load(path)
    obj = img[:, :, 0] < 2
    h, w = obj.shape
    solid = obj.tolist()
    seen = [[False] * w for _ in range(h)]
    holes = []
    edge = []
    for y in range(h):
        for x in range(w):
            if solid[y][x] or seen[y][x]:
                continue
            size = 0
            touches = False
            dq = deque([(y, x)])
            seen[y][x] = True
            while dq:
                cy, cx = dq.popleft()
                size += 1
                if cy == 0 or cx == 0 or cy == h - 1 or cx == w - 1:
                    touches = True
                for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < h and 0 <= nx < w and not seen[ny][nx] and not solid[ny][nx]:
                        seen[ny][nx] = True
                        dq.append((ny, nx))
            (edge if touches else holes).append(size)
    holes.sort(reverse=True)
    print("== sky holes in %s" % path)
    print("   trapped sky components: %d   total trapped %d px   largest %s"
          % (len(holes), sum(holes), holes[:6]))
    print("   open sky reaching the frame edge: %d px" % sum(edge))
