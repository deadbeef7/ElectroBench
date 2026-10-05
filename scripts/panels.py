"""Read the 5-panel diagnostic render of the sea shader.

Panels (left to right), each 152 px wide in a 760 px frame:
  0 slope |grad|   1 sunDiffuse   2 fresnel   3 reflected sky luma
  4 warm-slope add

For each panel it reports the luma histogram and, crucially, the block-to-block
step inside the water region. A term carrying the blotch has a high step; a
term that is smooth there does not. It also reports the correlation of each
term with the panel-4 pattern so the winner can be named.
"""
import sys
from collections import deque

import numpy as np

NAMES = ["slope|grad|", "sunDiffuse", "fresnel", "refl luma", "warmSlopeAdd"]


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def components(mask, minarea=60):
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    cur = 0
    sizes = []
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if lab[y0, x0]:
            continue
        cur += 1
        q = deque([(y0, x0)])
        lab[y0, x0] = cur
        n = 0
        while q:
            y, x = q.popleft()
            n += 1
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] \
                            and not lab[ny, nx]:
                        lab[ny, nx] = cur
                        q.append((ny, nx))
        sizes.append(n)
    return lab, np.array(sizes)


def main():
    a = load(sys.argv[1])
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    # horizon from the (black) sky panels: sky was forced to black in the tag
    # pass, so the first lit row is the sea top edge.
    litrow = (lum.max(axis=1) > 0.02)
    hy = int(np.argmax(litrow)) if litrow.any() else 0
    print("frame %dx%d, tag pass: sea starts at y=%d" % (w, h, hy))
    pw = w // 5
    panels = {}
    for i in range(5):
        panels[i] = lum[:, i * pw:(i + 1) * pw]

    print("\nper panel: mean / std over water, and block-to-block step (8px)")
    ref = None
    for i in range(5):
        p = panels[i][hy:]
        bs = 8
        nb = p.shape[1] // bs
        nbz = p.shape[0] // bs
        blk = p[:nbz * bs, :nb * bs].reshape(nbz, bs, nb, bs).mean(axis=(1, 3))
        dh = np.abs(np.diff(blk, axis=1)).ravel()
        dv = np.abs(np.diff(blk, axis=0)).ravel()
        st = np.concatenate([dh, dv])
        panels["b%d" % i] = blk
        print("  %d %-13s mean %.4f  std %.4f  p95 %.4f | step mean %.5f"
              "  p99 %.5f  frac>0.06 %.1f%%"
              % (i, NAMES[i], p.mean(), p.std(), np.percentile(p, 95),
                 st.mean(), np.percentile(st, 99), 100.0 * (st > 0.06).mean()))

    # Which panel's pattern matches which? Correlate the block maps.
    print("\nblock-map correlation between panels (which term shares the shape):")
    n = 5
    print("        " + "".join("%11s" % NAMES[j][:10] for j in range(n)))
    for i in range(n):
        row = []
        for j in range(n):
            a1 = panels["b%d" % i].ravel() - panels["b%d" % i].mean()
            b1 = panels["b%d" % j].ravel() - panels["b%d" % j].mean()
            d = np.linalg.norm(a1) * np.linalg.norm(b1)
            row.append("%11.3f" % (float(a1 @ b1 / d) if d else 0.0))
        print("%-8s%s" % (NAMES[i][:8], "".join(row)))

    # Blotch size in the warmSlopeAdd panel (and others): connected components
    # of "lit", i.e. above that panel's own p60.
    print("\nconnected-component scale of the 'lit' region per panel:")
    for i in range(n):
        m = panels[i][hy:] > np.percentile(panels[i][hy:], 62)
        lab, sizes = components(m)
        big = sizes[sizes >= 400]
        if big.size == 0:
            print("  %-13s no component >=400 px (fragmented)"
                  % NAMES[i])
            continue
        eq = np.sqrt(4.0 * big / np.pi)
        print("  %-13s %5d blobs>=400px, equivalent diameter "
              "median %5.1f px  p90 %5.1f px  max %5.1f px"
              % (NAMES[i], big.size, np.median(eq),
                 np.percentile(eq, 90), eq.max()))


main()