"""Blotchiness vs pixellation, measured at several spatial scales.

sea_check.py's block step uses 4 px blocks, which is a PIXELLATION proxy: it
cannot tell a large flat amoeba patch from fine glitter. The sea was reported as
"like noise" because the sunlit water broke into big smooth camo patches, so the
scale that matters is the LARGE one. Measuring the same luma step at 4, 8, 16,
32 and 64 px blocks separates the two:

  * fine scales up while large scales down = the giant patches were broken up
    into glitter rather than merely deleted. That is the goal.
  * everything up = more noise, which is not the goal.
  * everything down = the sun path was deleted, not repaired.

Each cell is the mean absolute luma step between adjacent blocks of that size,
as a percentage of the sun column's mean luma, so the scales are comparable.

Usage: multiscale.py FRAME [FRAME ...]
"""
import sys

import numpy as np

SIZES = (4, 8, 16, 32, 64)


def load(p):
    if p.endswith(".png"):
        from PIL import Image
        return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def main():
    frames = sys.argv[1:]
    out = {}
    for p in frames:
        a = load(p)
        h, w, _ = a.shape
        lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        col = np.convolve(lum.mean(axis=1), np.ones(5) / 5.0, mode="same")
        hy = int(np.argmin(np.diff(col[:int(h * 0.70)]))) + 1
        wat = lum[hy:]
        cs = wat.mean(axis=0)
        k = max(8, w // 10)
        hot = np.zeros(w, bool)
        hot[np.argsort(cs)[::-1][:k]] = True
        wat = wat[:, hot]
        wm = float(wat.mean())
        for bs in SIZES:
            ny, nx = wat.shape[0] // bs, wat.shape[1] // bs
            b = wat[:ny * bs, :nx * bs].reshape(ny, bs, nx, bs).mean(axis=(1, 3))
            st = np.concatenate([np.abs(np.diff(b, axis=1)).ravel(),
                                 np.abs(np.diff(b, axis=0)).ravel()])
            out[(p, bs)] = (float(st.mean()) / wm * 100.0, float(wm))

    print("sun column: mean |luma step| between adjacent blocks, "
          "as %% of column mean\n")
    print("%-22s %9s %s" % ("frame", "colmean",
                            "".join("%10s" % ("%dpx" % bs) for bs in SIZES)))
    for p in frames:
        nm = p.split("/")[-1]
        cells = "".join("%10.2f" % out[(p, bs)][0] for bs in SIZES)
        print("%-22s %9.4f %s" % (nm, out[(p, SIZES[0])][1], cells))
    print("\nleft = pixellation, right = blotch. Goal: fine scales may rise, "
          "the 32/64 px columns must fall.")


main()