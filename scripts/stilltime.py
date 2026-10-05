"""Which of the loop frames is the committed still?

The still is 1600x900 and the loop frames are 760x428; both are ~16:9 and the
camera is a deterministic function of sim time, so the still should be the same
frame as one of the loop frames. Matching them settles the shot time for free.
"""
import sys
import numpy as np
from PIL import Image


def lum(im):
    a = np.asarray(im.convert("RGB")).astype(np.float64)
    return a @ np.array([0.299, 0.587, 0.114])


def main():
    still = Image.open("docs/screenshots/lain_lines.png")
    gif = Image.open("docs/screenshots/lain_lines.gif")
    ref = lum(still.resize((760, 428), Image.LANCZOS))
    ref = (ref - ref.mean()) / (ref.std() + 1e-9)
    print("loop frames:", getattr(gif, "n_frames", "?"))
    scores = []
    for i in range(getattr(gif, "n_frames", 0)):
        gif.seek(i)
        fr = gif.convert("RGB")
        if fr.size != (760, 428):
            fr = fr.resize((760, 428), Image.LANCZOS)
        a = lum(fr)
        a = (a - a.mean()) / (a.std() + 1e-9)
        scores.append((float((a * ref).mean()), i, fr.size))
    for s, i, size in sorted(scores, reverse=True):
        print("frame %d size=%s  corr %.4f" % (i, size, s))


main()