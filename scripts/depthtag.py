"""Read the material + distance tag pass and name what is hanging in the sky.

The tag block in object_frag.glsl writes:
    R = 0        (a validity flag -- the sky never renders exactly 0)
    G = vMat*15  (17 materials, exact round trip)
    B = distance from the eye in whole metres

so one render answers "what is it" AND "how far away is it", which is the
difference between guessing at a silhouette and identifying an object: a pole
fitting at 12 m and a pylon insulator string at 140 m can have identical
shapes and only the distance separates them.

Usage: depthtag.py IMG [IMG ...] [--minarea N] [--x0 x1 y0 y1]
"""
import sys
from collections import deque

import numpy as np

MATS = ["PAINT", "WOOD", "GROUND", "ROAD", "LINE", "CABLE", "CERAMIC",
        "METAL", "WALL", "ROOF", "GLASS", "LEAF", "SHADOW", "GLOW", "STEEL",
        "KERB", "CONCRETE"]


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def decode(img):
    obj = img[:, :, 0] < 2
    mat = np.rint(img[:, :, 1].astype(np.float64) / 15.0).astype(np.int16)
    dist = img[:, :, 2].astype(np.float64)
    return obj, np.clip(mat, 0, 16), dist


def components(mask):
    h, w = mask.shape
    lab = np.zeros((h, w), dtype=np.int32)
    cur = 0
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if lab[y0, x0]:
            continue
        cur += 1
        q = deque([(y0, x0)])
        lab[y0, x0] = cur
        while q:
            y, x = q.popleft()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] \
                            and not lab[ny, nx]:
                        lab[ny, nx] = cur
                        q.append((ny, nx))
    return lab, cur


def main():
    args = sys.argv[1:]
    minarea, region = 20, (0, 10 ** 9, 0, 10 ** 9)
    if "--" in args:
        i = args.index("--")
        prm = args[i + 1:]
        args = args[:i]
        j = 0
        while j < len(prm):
            if prm[j] == "--minarea":
                minarea = int(prm[j + 1]); j += 2
            elif prm[j] == "--":
                region = tuple(int(v) for v in prm[j + 1:j + 5]); j += 5
            else:
                j += 1
    x0, x1, y0, y1 = region

    for p in args:
        img = load(p)
        h, w = img.shape[:2]
        obj, mat, dist = decode(img)
        lab, cur = components(obj)
        sizes = np.bincount(lab.ravel(), minlength=cur + 1)
        world = int(np.argmax(sizes[1:])) + 1
        print("\n=== %s %dx%d  world=%d (%d px, %.1f%%)  detached=%d" % (
            p, w, h, world, sizes[world], 100.0 * sizes[world] / obj.size,
            cur - 1))
        rows = []
        for c in range(1, cur + 1):
            if c == world or sizes[c] < minarea:
                continue
            ys, xs = np.nonzero(lab == c)
            bx = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
            if bx[2] < x0 or bx[0] > x1 or bx[3] < y0 or bx[1] > y1:
                continue
            ms = mat[ys, xs]
            ds = dist[ys, xs]
            mix = {}
            for v, n in zip(*np.unique(ms, return_counts=True)):
                mix[MATS[v]] = int(n)
            rows.append((int(sizes[c]), bx, mix, float(ds.mean()),
                         int(ds.min()), int(ds.max())))
        rows.sort(key=lambda r: -r[0])
        for n, bx, mix, dm, d0, d1 in rows:
            bw, bh = bx[2] - bx[0] + 1, bx[3] - bx[1] + 1
            print("  n=%-5d box x%d-%d y%d-%d (%dx%d) dist %.1fm [%d-%d]  %s" % (
                n, bx[0], bx[2], bx[1], bx[3], bw, bh, dm, d0, d1,
                " ".join("%s:%d" % kv for kv in sorted(mix.items(),
                                                      key=lambda kv: -kv[1]))))


if __name__ == "__main__":
    main()