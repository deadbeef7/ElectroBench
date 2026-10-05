"""Characterise the 'noise' blotches on the sunlit sea in a frame.

Identifies connected components of the tan/khaki (sunlit) water and reports
their size, colour and edge hardness, so the shader term responsible can be
named from geometry rather than guessed.
"""
import sys
import numpy as np

try:
    from scipy import ndimage
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


def load(p):
    from PIL import Image
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0


def main():
    a = load(sys.argv[1])
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]

    col = lum.mean(axis=1)
    d = np.diff(np.convolve(col, np.ones(5) / 5.0, mode="same")[:int(h * 0.70)])
    hy = int(np.argmin(d)) + 1
    print("image %dx%d  horizon ~y%d" % (w, h, hy))
    water = np.zeros((h, w), bool)
    water[hy:] = True

    # Sunlit water = warm and bright: red leads blue clearly, and it is not the
    # pale sky. Restrict to the water rows.
    warm = (r - b > 0.10) & (lum > 0.28) & water
    print("warm+sunlit water px: %d (%.2f%% of water)"
          % (warm.sum(), 100.0 * warm.sum() / water.sum()))
    if warm.sum() == 0:
        return

    ys, xs = np.where(warm)
    print("  y %d..%d  x %d..%d" % (ys.min(), ys.max(), xs.min(), xs.max()))
    print("  mean colour r=%.3f g=%.3f b=%.3f  mean luma %.3f"
          % (r[warm].mean(), g[warm].mean(), b[warm].mean(), lum[warm].mean()))
    # how flat is it inside? low std => a flat painted patch, not shading
    print("  luma std inside %.4f   p05 %.3f p50 %.3f p95 %.3f"
          % (lum[warm].std(), np.percentile(lum[warm], 5),
             np.percentile(lum[warm], 50), np.percentile(lum[warm], 95)))

    if not HAVE_SCIPY:
        print("(no scipy: skipping component analysis)")
        return

    lab, n = ndimage.label(warm)
    print("\nconnected sunlit components: %d" % n)
    sizes = ndimage.sum(warm, lab, range(1, n + 1))
    order = np.argsort(sizes)[::-1]
    print("  largest 12 sizes (px): %s"
          % [int(sizes[i]) for i in order[:12]])
    tot = sizes.sum()
    print("  share of sunlit px in top 10 components: %.1f%%"
          % (100.0 * sizes[order[:10]].sum() / tot))
    # equivalent diameter of the biggest blob
    for i in order[:5]:
        cid = i + 1
        sl = ndimage.find_objects(lab == cid)[0]
        print("    blob %d: %8d px  bbox y%d..%d x%d..%d  (%dx%d)"
              % (cid, sizes[i], sl[0].start, sl[0].stop,
                 sl[1].start, sl[1].stop,
                 sl[1].stop - sl[1].start, sl[0].stop - sl[0].start))

    # Edge sharpness of the lit/dark boundary: a hard smoothstep boundary shows
    # up as a very steep luma ramp; a soft/noisy one as a gradual one.
    gx = np.abs(np.diff(lum, axis=1))
    gy = np.abs(np.diff(lum, axis=0))
    gm = np.zeros((h, w), bool)
    gm[:-1, :-1] = warm[:-1, :-1] ^ warm[1:, 1:]
    sel = gm & water[:-1, :-1]
    print("\nboundary pixels: %d" % sel.sum())
    if sel.sum():
        e = np.concatenate([gx[sel], gy[sel]])
        print("  |d luma| across boundary: mean %.4f p50 %.4f p90 %.4f p99 %.4f"
              % (e.mean(), np.percentile(e, 50), np.percentile(e, 90),
                 np.percentile(e, 99)))
        print("  frac of boundary steps > 0.05: %.1f%%"
              % (100.0 * (e > 0.05).mean()))

    # Radial profile: is the lit area concentrated in a narrow sun column,
    # or spread across the whole sea (which would mean the term is not gated
    # by sunAlign at all)?
    print("\nsunlit/lit coverage by water row band:")
    for y0 in range(hy, h, max(1, (h - hy) // 8)):
        y1 = min(h + 1, y0 + max(1, (h - hy) // 8))
        m = np.zeros((h, w), bool)
        m[y0:y1, :] = True
        mm = m & water
        if mm.sum() == 0:
            continue
        print("   y%4d..%4d  lit %6.2f%%  mean luma %.3f"
              % (y0, y1 - 1, 100.0 * (warm & mm).sum() / mm.sum(),
                 lum[mm].mean()))


main()