"""Measure the three reported sea defects in a scene-2 frame.

 1. glitter path too bright  -> clip rate + high-luma share inside the sun column
 2. glitter path pixellated  -> block-to-block luma jumps inside the column
 3. light blue spots in sea  -> pixels where blue clearly leads red AND green,
                                restricted to the water region (below horizon)

Usage: sea_check.py <ppm|png>
"""
import sys
import numpy as np


def load(p):
    if p.endswith(".png"):
        from PIL import Image
        a = np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0
        return a
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def main():
    a = load(sys.argv[1])
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    sat = (a.max(axis=2) - a.min(axis=2))

    # horizon: strongest downward jump in row-mean luma within the upper 70%.
    # The sky is bright and the sea is dark, so the sea's top edge is the one
    # place the profile steps DOWN hard.
    col = lum.mean(axis=1)
    k3 = np.ones(5) / 5.0
    cs = np.convolve(col, k3, mode="same")
    d = np.diff(cs[: int(h * 0.70)])
    hy = int(np.argmin(d)) + 1
    print("image %dx%d  horizon~y%d  (row luma %.3f -> %.3f)"
          % (w, h, hy, cs[hy - 1], cs[min(hy + 1, h - 1)]))

    water = np.zeros((h, w), bool)
    water[hy:] = True
    sky = ~water

    # ---- the sun column: brightest columns of the water region ----
    wl = lum * water
    colscore = wl[hy:].mean(axis=0)
    k = max(8, w // 10)
    order = np.argsort(colscore)[::-1]
    hot = np.zeros(w, bool)
    hot[order[:k]] = True
    colmask = np.zeros((h, w), bool)
    colmask[hy:, :] = hot[None, :]

    print("\n--- 1/2: sun column (water, brightest %d/%d cols) ---" % (k, w))
    cl = lum[colmask]
    print("  mean luma      %.4f" % cl.mean())
    print("  p99  luma      %.4f" % np.percentile(cl, 99))
    print("  max   luma      %.4f" % cl.max())
    print("  >0.97 (clip)   %.3f%%" % (100.0 * (cl > 0.97).mean()))
    print("  >0.90          %.3f%%" % (100.0 * (cl > 0.90).mean()))
    print("  >0.80          %.3f%%" % (100.0 * (cl > 0.80).mean()))

    # pixellation: mean |luma difference| between adjacent 4x4 blocks
    bs = 4
    y0 = (hy // bs) * bs
    nbx, nby = w // bs, (h - y0) // bs
    blocks = lum[y0:y0 + nby * bs, :nbx * bs].reshape(nby, bs, nbx, bs).mean(axis=(1, 3))
    dh = np.abs(np.diff(blocks, axis=1))          # (nby, nbx-1)
    dv = np.abs(np.diff(blocks, axis=0))          # (nby-1, nbx)
    bmask = np.zeros(blocks.shape, bool)
    bmask[:, hot[:nbx * bs].reshape(nbx, bs).any(axis=1)] = True
    steps = np.concatenate([dh[bmask[:, :-1]].ravel(),
                            dv[bmask[:-1, :]].ravel()])
    print("  block-to-block luma step (pixellation proxy)")
    print("    mean  %.5f" % steps.mean())
    print("    p99   %.5f" % np.percentile(steps, 99))
    print("    frac > 0.03   %.2f%%" % (100.0 * (steps > 0.03).mean()))
    print("    frac > 0.06   %.2f%%" % (100.0 * (steps > 0.06).mean()))

    # ---- 3: blue spots ----
    print("\n--- 3: blue-leading pixels ---")
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    bluish = (b - r > 0.04) & (b - g > 0.02)
    for name, m in (("whole frame", np.ones((h, w), bool)),
                    ("water", water),
                    ("sun column", colmask)):
        n = int((bluish & m).sum())
        print("  %-11s bluish %7d px  %6.3f%%" % (name, n, 100.0 * n / m.sum()))
    strong = bluish & water & (lum > np.percentile(lum[water], 60))
    print("  water & bluish & above p60 luma: %d px" % int(strong.sum()))
    if strong.sum():
        print("    mean colour of those: r=%.3f g=%.3f b=%.3f" %
              (r[strong].mean(), g[strong].mean(), b[strong].mean()))
        ys, xs = np.where(strong)
        print("    y range %d..%d  x range %d..%d" %
              (ys.min(), ys.max(), xs.min(), xs.max()))


main()
