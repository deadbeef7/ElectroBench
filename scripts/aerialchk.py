"""Compare the old committed still against a new render in the aerial region.

Reports, for a crop window: an ASCII luma map of each image, the pixels that
changed, and whether the mast foot now meets roof mass or floats above it.
"""
import sys
import numpy as np
from PIL import Image


def load_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        data = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    # WriteScreenshotPPM walks glReadPixels rows from h-1 down to 0, so the file
    # is already top-down. PIL agrees; do NOT flip.
    return data.reshape(h, w, 3)


def load(path):
    if path.endswith(".ppm"):
        return load_ppm(path)
    return np.asarray(Image.open(path).convert("RGB"))


RAMP = " .:-=+*#%@"


def ascii_map(img, x0, x1, y0, y1):
    sub = img[y0:y1, x0:x1].astype(np.float64)
    lum = sub @ np.array([0.299, 0.587, 0.114])
    lo, hi = np.percentile(lum, 2), np.percentile(lum, 98)
    n = (lum - lo) / max(hi - lo, 1e-6)
    idx = np.clip((n * (len(RAMP) - 1)).astype(int), 0, len(RAMP) - 1)
    return ["".join(RAMP[v] for v in row) for row in idx], lum


def sky_hue(img):
    """Mean hue of the top strip, the same measurement the P16 note quotes."""
    h = img[: int(img.shape[0] * 0.18)]
    px = h.reshape(-1, 3).astype(np.float64) / 255.0
    mx = px.max(axis=1)
    mn = px.min(axis=1)
    v = mx
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-9), 0.0)
    d = mx - mn
    r, g, b = px[:, 0], px[:, 1], px[:, 2]
    hue = np.zeros_like(mx)
    nz = d > 1e-9
    idx = nz & (mx == r)
    hue[idx] = ((g - b)[idx] / d[idx]) % 6
    idx = nz & (mx == g)
    hue[idx] = ((b - r)[idx] / d[idx]) + 2
    idx = nz & (mx == b)
    hue[idx] = ((r - g)[idx] / d[idx]) + 4
    hue *= 60.0
    order = np.argsort(hue)
    hs = hue[order]
    cum = np.cumsum(hs)
    # circular mean, centred so 0deg is up
    ang = np.deg2rad(hs)
    m = np.arctan2(np.sin(ang).mean(), np.cos(ang).mean())
    deg = np.rad2deg(m) % 360.0
    return deg, s.mean(), (v > 0.97).mean() * 100.0, (v < 0.02).mean() * 100.0


def main():
    new_path, old_path = sys.argv[1], sys.argv[2]
    x0, x1, y0, y1 = (int(v) for v in sys.argv[3:7])
    new = load(new_path)
    old = load(old_path)
    print("new", new.shape, " old", old.shape)

    for name, img in (("OLD", old), ("NEW", new)):
        rows, _ = ascii_map(img, x0, x1, y0, y1)
        print("\n%s  x%d-%d y%d-%d" % (name, x0, x1, y0, y1))
        print("     " + "".join(str((x0 + i) // 10 % 10) for i in range(x1 - x0)))
        for j, r in enumerate(rows):
            print("%4d %s" % (y0 + j, r))

    # where did the pixels change?
    h = min(new.shape[0], old.shape[0])
    w = min(new.shape[1], old.shape[1])
    diff = np.abs(new[:h, :w].astype(np.int16) - old[:h, :w].astype(np.int16))
    print("\nper-channel |diff| mean %.2f  p99 %d  max %d" %
          (diff.mean(), np.percentile(diff, 99), diff.max()))
    d = diff.max(axis=2)
    for thr in (4, 12, 40):
        print("  px with max-channel diff > %d: %d (%.2f%%)" %
              (thr, int((d > thr).sum()), 100.0 * (d > thr).mean()))
    ys, xs = np.nonzero(d > 12)
    if len(xs):
        print("\nchanged px: %d  bbox x%d-%d y%d-%d" %
              (len(xs), xs.min(), xs.max(), ys.min(), ys.max()))
        # cluster the change by x so we can see which object moved
        hist = np.bincount(xs, minlength=w)
        runs, start = [], None
        for i in range(w):
            if hist[i] and start is None:
                start = i
            elif not hist[i] and start is not None:
                runs.append((start, i - 1, int(hist[start:i].sum())))
                start = None
        if start is not None:
            runs.append((start, w - 1, int(hist[start:].sum())))
        print("change runs (x0,x1,px):", [r for r in runs if r[2] > 20])
    else:
        print("\nchanged px: 0")

    for name, img in (("OLD", old), ("NEW", new)):
        deg, sat, clip, black = sky_hue(img)
        print("%s sky hue %.1f deg  sat %.3f  clip %.2f%%  black %.2f%%" %
              (name, deg, sat, clip, black))


main()