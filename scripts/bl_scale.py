"""How does the blotch scale with depth in the REAL 1280x720 frame?

Two competing explanations, and they predict different things:

  (a) the long swells (lambda 37 m / 26 m) painting sun-facing faces. Those are
      resolved at every distance, so the blobs stay at full contrast all the way
      out but SHRINK on screen as the water recedes.

  (b) a texture tile (foam tiles every 178 m, chaos every 21.6 m) at full
      contrast. Same shrinkage, but the scale should snap to the tile's world
      size at specific distances.

So: measure the block-to-block luma step AND the dominant blob scale, per depth
band, in the committed screenshot. Also report the camera geometry so each band
can be converted to a distance in metres.
"""
import sys

import numpy as np


def load(p):
    from PIL import Image
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0


def dominant_scale(patch):
    """Feature size of a band: first zero crossing of the autocorrelation is a
    robust correlation length; the strongest ACF peak is the dominant period."""
    row = patch.mean(axis=0)
    row = row - row.mean()
    n = len(row)
    f = np.fft.rfft(row, n=2 * n)
    ac = np.fft.irfft(f * np.conj(f))[:n].real
    if ac[0] <= 0:
        return 0.0, 0.0
    ac /= ac[0]
    # first zero crossing
    zc = n - 1
    for i in range(1, n):
        if ac[i] <= 0.0:
            zc = i
            break
    # strongest interior peak
    best, bestlag = 0.0, 0
    for lag in range(8, min(300, n - 1)):
        if ac[lag] > ac[lag - 1] and ac[lag] >= ac[lag + 1] and ac[lag] > best:
            best, bestlag = ac[lag], lag
    if bestlag == 0:
        bestlag = zc
    return float(bestlag), float(max(best, 0.0))


def main():
    a = load(sys.argv[1])
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)

    # horizon: strongest downward luma step in the upper 70%
    col = np.convolve(lum.mean(axis=1), np.ones(5) / 5.0, mode="same")
    hy = int(np.argmin(np.diff(col[:int(h * 0.70)]))) + 1
    print("frame %dx%d  horizon row %d" % (w, h, hy))

    # camera geometry from scene2.cxx: eye y = 7 m, pitch -0.05 rad, vFOV 45.
    # Ray elevation = pitch + atan(ndc_y * tan(vfov/2)); ndc_y = 1 - 2r/(h-1).
    eye_y, pitch, vfov = 7.0, -0.05, 45.0
    tv = np.tan(np.radians(vfov) * 0.5)

    def row_dist(r):
        ndc = 1.0 - 2.0 * np.asarray(r, dtype=float) / (h - 1.0)
        elev = pitch + np.arctan(ndc * tv)
        return eye_y / np.tan(np.clip(-elev, 1e-5, None))

    print("row %d = %.0f m   row %d = %.0f m   row %d = %.1f m"
          % (hy, row_dist(hy), h - 1, row_dist(h - 1), (hy + h) // 2,
             row_dist((hy + h) // 2)))

    bs = 4
    y0 = (hy // bs) * bs
    nbx, nby = w // bs, (h - y0) // bs
    blocks = lum[y0:y0 + nby * bs, :nbx * bs].reshape(nby, bs, nbx, bs).mean(axis=(1, 3))

    print("\n depth band      dist m   block step   step/mean   blob px   ACG   "
          "world size m (x by y)")
    for k in range(8):
        by0 = k * nby // 8
        by1 = (k + 1) * nby // 8
        if by1 - by0 < 3:
            continue
        band = blocks[by0:by1]
        st = np.concatenate([np.abs(np.diff(band, axis=1)).ravel(),
                             np.abs(np.diff(band, axis=0)).ravel()])
        r0 = y0 + by0 * bs
        r1 = y0 + by1 * bs - 1
        dm = float(row_dist((r0 + r1) // 2))
        dep = np.arctan(eye_y / max(dm, 1e-3))      # grazing compression
        # horizontal m/px from the perspective divide; vertical is stretched by
        # 1/sin(depression) because the surface recedes.
        mpp_x = 2.0 * dm * tv / w * (w / h)
        mpp_y = mpp_x / max(np.sin(dep), 1e-3)
        lag, acg = dominant_scale(lum[r0:r1 + 1])
        print("  rows %3d-%3d  %8.1f   %.5f    %6.1f%%   %6.1f   %.2f   "
              "%.1f x %.1f"
              % (r0, r1, dm, st.mean(), 100.0 * st.mean() / band.mean(),
                 lag, acg, lag * bs * mpp_x, lag * bs * mpp_y))

    print("\nfoam tile = 4096/23 = 178.3 m, base feature 22.3 m, streak 14.3 x 35.7 m")
    print("chaos tile = 4096/190 = 21.6 m")
    print("swells: 37.0 m, 26.2 m, 16.5 m")


main()