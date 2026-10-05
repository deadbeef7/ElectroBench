"""Find geometry that hangs in the air in front of the TREELINE.

The earlier detectors all had one blind spot, and the user's pin walked straight
into it: `float.py` demanded open sky *underneath* a fragment, so anything
silhouetted against the trees -- which is exactly where the user sees these --
was invisible to it. This one drops the sky-underneath rule entirely.

The tag render (see object_frag.glsl BUILD-P16 tag block) paints every material
a flat, exact colour and leaves the sky on its own shader, so "is there geometry
here at all" is an exact palette lookup rather than a brightness threshold, and
LEAF is separable from METAL so a tree can never be mistaken for hardware.

Method:
  * label the connected components of ALL geometry -- one world, everything else
    is unattached by construction;
  * label the components of each MATERIAL on its own, so a cylinder crossing a
    wall or a foliage mass still comes out whole;
  * report bbox, class mix, gap to the world (in px of empty sky), and how much
    of the component's immediate surround is foliage vs open sky.

Usage: treeline.py IMG [IMG ...] [-- minarea N] [-- x0 x1 y0 y1]
"""
import sys
from collections import deque

import numpy as np

PAL = {
    (255, 0, 255): "CABLE", (0, 255, 255): "CERAMIC", (0, 255, 0): "METAL",
    (255, 0, 0): "WALL", (160, 0, 0): "ROOF", (255, 0, 128): "GLASS",
    (0, 110, 0): "LEAF", (255, 255, 0): "SHADOW", (120, 120, 0): "GLOW",
    (255, 140, 0): "STEEL", (150, 0, 255): "KERB", (0, 120, 255): "CONCRETE",
    (64, 64, 64): "OTHER",
}
NAMES = ["SKY"] + sorted(set(PAL.values()))


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def classify(img):
    """Exact palette lookup -> class index, SKY where nothing was tagged."""
    out = np.zeros(img.shape[:2], dtype=np.int8)  # 0 == SKY
    for rgb, name in PAL.items():
        m = (img[:, :, 0] == rgb[0]) & (img[:, :, 1] == rgb[1]) & \
            (img[:, :, 2] == rgb[2])
        out[m] = NAMES.index(name)
    return out


def components(mask):
    """8-connected labelling, flood fill. Returns (labels, count)."""
    h, w = mask.shape
    lab = np.zeros((h, w), dtype=np.int32)
    cur = 0
    m = mask
    ys, xs = np.nonzero(m)
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
                    if 0 <= ny < h and 0 <= nx < w and m[ny, nx] \
                            and not lab[ny, nx]:
                        lab[ny, nx] = cur
                        q.append((ny, nx))
    return lab, cur


def dilate(mask, k):
    out = mask.copy()
    for _ in range(k):
        g = out.copy()
        g[1:] |= out[:-1]
        g[:-1] |= out[1:]
        g[:, 1:] |= out[:, :-1]
        g[:, :-1] |= out[:, 1:]
        out = g
    return out


def describe(lab, cur, world, sel, cls, gapmax=8):
    """For every label in `sel`, report bbox / class mix / gap to the world.

    `lab` supplies the shapes, `cls` the materials -- they are different things
    and mixing them up indexes the name table with a label id.
    """
    rows = []
    wl = lab == world
    grown = [dilate(wl, k) for k in range(gapmax + 1)]
    for c in sel:
        ys, xs = np.nonzero(lab == c)
        if len(ys) == 0:
            continue
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        gap = -1
        for k in range(gapmax + 1):
            if grown[k][ys, xs].any():
                gap = k
                break
        if gap < 0:
            gap = gapmax + 1  # further than the ladder reaches: fully adrift
        sub = cls[ys, xs]
        mix = {}
        for v in np.unique(sub):
            mix[NAMES[v]] = int((sub == v).sum())
        rows.append(dict(n=int(len(ys)), box=(int(x0), int(y0), int(x1),
                                               int(y1)), gap=gap, mix=mix))
    return rows


def main():
    args = sys.argv[1:]
    minarea, region = 40, (0, 10 ** 9, 0, 10 ** 9)
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
        cls = classify(img)
        obj = cls > 0
        lab, cur = components(obj)
        sizes = np.bincount(lab.ravel(), minlength=cur + 1)
        # label 0 is background, so the world is the largest real component
        world = int(np.argmax(sizes[1:])) + 1

        print("\n=== %s  %dx%d  world=%d (%d px, %.1f%%)  parts=%d" % (
            p, w, h, world, sizes[world], 100.0 * sizes[world] / (h * w),
            cur - 1))

        # 1) unattached geometry of any class
        rows = describe(lab, cur, world, [c for c in range(1, cur + 1)
                                         if c != world and sizes[c] >= minarea],
                         cls)
        seen = {tuple(r["box"]) for r in rows}
        rows.sort(key=lambda r: -r["n"])
        for r in rows:
            bx = r["box"]
            if bx[2] < x0 or bx[0] > x1 or bx[3] < y0 or bx[1] > y1:
                continue
            bw, bh = bx[2] - bx[0] + 1, bx[3] - bx[1] + 1
            print("  PART n=%-6d box x%d-%d y%d-%d (%dx%d) gap=%s  %s" % (
                r["n"], bx[0], bx[2], bx[1], bx[3], bw, bh,
                "far>%dpx" % gapmax if r["gap"] < 0 else "%dpx" % r["gap"],
                " ".join("%s:%d" % kv for kv in sorted(
                    r["mix"].items(), key=lambda kv: -kv[1]))))

        # 2) per-material, so hardware crossing foliage or a wall stays whole
        print("  -- per material, components >= %d px not touching the world --"
              % minarea)
        for ci in range(1, len(NAMES)):
            m = cls == ci
            if not m.any():
                continue
            l2, c2 = components(m)
            s2 = np.bincount(l2.ravel(), minlength=c2 + 1)
            # the world of THIS material is its own biggest component
            w2 = int(np.argmax(s2[1:])) + 1
            for r in describe(l2, c2, w2, [c for c in range(1, c2 + 1)
                                           if c != w2 and s2[c] >= minarea],
                             cls):
                bx = r["box"]
                if bx[2] < x0 or bx[0] > x1 or bx[3] < y0 or bx[1] > y1:
                    continue
                bw, bh = bx[2] - bx[0] + 1, bx[3] - bx[1] + 1
                # how much foliage / sky touches the component's edge
                b = np.zeros((h, w), bool)
                b[max(0, bx[1] - 2):bx[3] + 3, max(0, bx[0] - 2):bx[2] + 3] = 1
                ring = b & ~(m | dilate(m, 2))
                leaf = int((ring & (cls == NAMES.index("LEAF"))).sum())
                sky = int((ring & (cls == 0)).sum())
                near = max(1, leaf + sky)
                print("     %-9s n=%-6d box x%d-%d y%d-%d (%dx%d) "
                      "surround leaf %d sky %d" % (
                          NAMES[ci], r["n"], bx[0], bx[2], bx[1], bx[3], bw, bh,
                          100 * leaf // near, 100 * sky // near))


if __name__ == "__main__":
    main()