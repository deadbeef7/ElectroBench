#!/usr/bin/env python3
"""Definitive floating-geometry test on the flat-sky debug render.

Everything anchored to the world is reachable, pixel by pixel, from the bottom
row of the frame (the ground). Anything NOT reachable is, by definition, not
attached to anything: a hanging cylinder, an unterminated stub, a coil whose
bracket is missing. This is a connectivity argument, not a heuristic, so it has
no false positives from wires, poles or the wire web.

  unreached.py in.ppm bg_r bg_g bg_b [thresh]
"""
import sys
import numpy as np
from PIL import Image
from collections import deque


def main(inp, br, bg, bb, thresh=26):
    a = np.asarray(Image.open(inp).convert("RGB")).astype(np.int16)
    d = np.abs(a - np.array([int(br), int(bg), int(bb)], np.int16)).sum(axis=2)
    mask = d > thresh
    h, w = mask.shape
    reach = np.zeros_like(mask)
    q = deque()
    for x in range(w):
        if mask[h - 1, x]:
            reach[h - 1, x] = True
            q.append((h - 1, x))
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not reach[ny, nx]:
                reach[ny, nx] = True
                q.append((ny, nx))
    float_mask = mask & ~reach
    n = int(float_mask.sum())
    print(f"{inp}: geometry {int(mask.sum())} px, anchored {int(reach.sum())} px, "
          f"FLOATING {n} px ({100.0*n/max(1,int(mask.sum())):.2f}% of geometry)")
    if not n:
        return
    lab = np.zeros((h, w), np.int32)
    cur = 0
    comps = []
    ys, xs = np.where(float_mask)
    for y0, x0 in zip(ys, xs):
        if lab[y0, x0]:
            continue
        cur += 1
        dq = deque([(y0, x0)])
        lab[y0, x0] = cur
        pix = []
        while dq:
            y, x = dq.popleft()
            pix.append((y, x))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if (0 <= ny < h and 0 <= nx < w and float_mask[ny, nx]
                        and not lab[ny, nx]):
                    lab[ny, nx] = cur
                    dq.append((ny, nx))
        pa = np.array(pix)
        yy, xx = pa[:, 0], pa[:, 1]
        bw = int(xx.max() - xx.min() + 1)
        bh = int(yy.max() - yy.min() + 1)
        comps.append((len(pix), bw, bh, bh / bw, int(xx.min()), int(yy.min()),
                      int(xx.max()), int(yy.max())))
    comps.sort(key=lambda c: -c[0])
    print(f"unanchored components: {len(comps)}")
    for (c, bw, bh, asp, x0, y0, x1, y1) in comps[:24]:
        print(f"  area={c:6d} {bw:3d}x{bh:3d} h/w={asp:6.2f} "
              f"at ({x0},{y0})-({x1},{y1})")
    out = np.where(float_mask[..., None], np.uint8(20),
                   np.uint8(245)).astype(np.uint8)
    Image.fromarray(np.concatenate([out] * 3, axis=2)).save(
        inp.rsplit(".", 1)[0] + "_float.png")
    print(f"  -> {inp.rsplit('.', 1)[0]}_float.png")


if __name__ == "__main__":
    main(*sys.argv[1:])
