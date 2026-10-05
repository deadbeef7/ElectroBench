"""Write docs/screenshots/uzi_flyover.gif from the ten rendered PPMs.

The README promises "ten frames across the run", so the frame count is an
assertion, not a default: the file this replaces shipped with ONE frame, which
is why it is built this way. 760x428 to match the other two loops, 256-colour
adaptive palette taken from the first frame, Floyd-Steinberg dither on the
rest, loop=0.

1200 ms a frame (12 s for the loop) rather than the 900 ms the two four-frame
dolly loops use: this one spans the whole 47 s three-act camera move, and at
900 ms it reads as a slideshow rather than a glide.
"""
import sys
import numpy as np
from PIL import Image

OUT = "docs/screenshots/uzi_flyover.gif"
TIMES = ["2", "5", "7.5", "12", "17", "22", "27", "34.5", "40", "45"]


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return Image.fromarray(d.reshape(h, w, 3))


def main():
    root = sys.argv[1]
    frames = [load("%s/f%s.ppm" % (root, t)) for t in TIMES]
    for f, t in zip(frames, TIMES):
        assert f.size == (760, 428), (t, f.size)
    assert len(frames) == 10
    pal = frames[0].convert("P", palette=Image.ADAPTIVE, colors=256)
    frames[0] = pal
    for f in frames[1:]:
        f.quantize(palette=pal, dither=Image.FLOYDSTEINBERG)
    frames[0].save(OUT, save_all=True, append_images=frames[1:],
                   duration=1200, loop=0, optimize=True)
    print("wrote %s 760x428 x%d" % (OUT, len(frames)))


main()
