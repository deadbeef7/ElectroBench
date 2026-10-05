"""Correlate an arbitrary render against the committed loop frames + still."""
import sys
import numpy as np
from PIL import Image


def lum(im):
    a = np.asarray(im.convert("RGB")).astype(np.float64)
    return a @ np.array([0.299, 0.587, 0.114])


def corr(a, b):
    a = (a - a.mean()) / (a.std() + 1e-9)
    b = (b - b.mean()) / (b.std() + 1e-9)
    return float((a * b).mean())


def fit(im):
    if im.size != (760, 428):
        im = im.resize((760, 428), Image.LANCZOS)
    return lum(im)


def main():
    cand = Image.open(sys.argv[1])
    c = fit(cand)
    still = Image.open("docs/screenshots/lain_lines.png")
    print("candidate", sys.argv[1], cand.size,
          " corr vs still(1600->760) %.4f" % corr(c, fit(still)))
    gif = Image.open("docs/screenshots/lain_lines.gif")
    for i in range(gif.n_frames):
        gif.seek(i)
        print("  vs loop frame %d: %.4f" % (i, corr(c, fit(gif.convert("RGB")))))


main()