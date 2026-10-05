"""Water brightness across sea variants: where did the sun path go?

sea_check.py reports the sun COLUMN (the brightest 10% of columns). A fix that
merely relights the sea and a fix that deletes the sun path both look like a
falling column mean, so this separates them:

  * whole-water mean luma   -- did the sea darken at all?
  * peak sun-path luma      -- is the path still there?
  * path width at half peak -- did it collapse to a thread?
  * p50/p90 of the water    -- the bulk, not the highlights
"""
import sys

import numpy as np


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
    print("%-26s %8s %8s %8s %8s %8s %8s"
          % ("frame", "water", "p50", "p90", "peak", "halfwid", "sky"))
    for p in sys.argv[1:]:
        a = load(p)
        h, w, _ = a.shape
        lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        col = np.convolve(lum.mean(axis=1), np.ones(5) / 5.0, mode="same")
        hy = int(np.argmin(np.diff(col[:int(h * 0.70)]))) + 1
        wat = lum[hy:]
        sky = lum[:hy]
        # sun path: mean luma per water column
        cs = wat.mean(axis=0)
        peak = float(cs.max())
        half = np.nonzero(cs > 0.5 * peak)[0]
        width = float(half.size) if half.size else 0.0
        name = p.split("/")[-1]
        print("%-26s %8.4f %8.4f %8.4f %8.4f %8.1f %8.4f"
              % (name, wat.mean(), np.percentile(wat, 50),
                 np.percentile(wat, 90), peak, width, sky.mean()))


main()