"""Write the committed still and both loops from the rendered PPMs.

The still is 1280x720 PNG; the scene 4 loop is 760x428 x 4 frames and the
scene 3 loop is 760x427 x 4 (the two scenes derive their height from
different formulas, so the rows differ by one). Both are palettised to a
256-colour adaptive palette, 900 ms a frame, looping forever -- the same
parameters as the GIFs the repository already carries.
"""
import sys
import numpy as np
from PIL import Image

STILL = "docs/screenshots/lain_lines.png"
LOOP = "docs/screenshots/lain_lines.gif"
POOL_LOOP = "docs/screenshots/pool_splash.gif"


def build_loop(paths, out, size):
    frames = [load(p) for p in paths]
    for f in frames:
        assert f.size == size, (out, f.size)
    pal = frames[0].convert("P", palette=Image.ADAPTIVE, colors=256)
    frames[0] = pal
    for f in frames[1:]:
        f.quantize(palette=pal, dither=Image.FLOYDSTEINBERG)
    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=900, loop=0, optimize=True)
    print("wrote %s %dx%d x%d" % (out, size[0], size[1], len(frames)))


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return Image.fromarray(d.reshape(h, w, 3))  # file is already top-down


def main():
    args = sys.argv[1:]
    still_p = args[0]
    # "-" means "leave the committed still alone": re-rendering the loops must
    # not demote the 1280x720 still to the 760px loop width.
    if still_p != "-":
        s = load(still_p)
        # BUILD-P20: the bench now presents at 1280x720, so the committed still
        # is the real presentation frame rather than an upscaled 1600x900 render.
        assert s.size == (1280, 720), s.size
        s.save(STILL)
        print("wrote %s %dx%d" % (STILL, *s.size))

    build_loop(args[1:5], LOOP, (760, 428))
    build_loop(args[5:9], POOL_LOOP, (760, 427))


main()