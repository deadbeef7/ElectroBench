#!/usr/bin/env python3
"""Count cylinder-shaped blobs per material class in a MATERIAL TAG render.

Tag the object shader by vMat (one saturated colour per material), render, and
count tall narrow blobs per colour. vMat is a flat varying and the shader is
loaded at runtime, so this needs no rebuild. A tall narrow blob well clear of
the horizon in one colour is a "flying cylinder" of that class.

  cablebars.py in.ppm [bg_r bg_g bg_b]
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from skydet import label  # noqa: E402

SKY = (107, 117, 127)          # flat-sky debug background
TAGS = {
    "cable(5)": (255, 0, 255), "ceramic(6)": (0, 255, 255), "metal(7)": (0, 255, 0),
    "steel(14)": (0, 0, 255), "concrete(16)": (255, 255, 255),
    "wall/roof/glass/leaf(8-11)": (77, 77, 140),
    # kMatGlow(13) is yellow; the DIAGNOSTIC render repurposes it to separate
    # the telecom bundles from every other cable user.
    "glow/telecom(12/13)": (255, 255, 0),
}


def blobs(mask, hy, min_h=8, min_hw=1.5, min_n=8):
    sky = mask.copy()
    sky[hy:, :] = False
    sky[:2, :] = False
    lab = label(sky)
    sizes = np.bincount(lab.ravel())
    out = []
    for i in range(1, len(sizes)):
        if sizes[i] < min_n:
            continue
        ys, xs = np.nonzero(lab == i)
        cw = xs.max() - xs.min() + 1
        ch = ys.max() - ys.min() + 1
        if ch >= min_h and ch > min_hw * cw:
            out.append((int(sizes[i]), int(cw), int(ch), int(xs.min()), int(ys.min()),
                        int(ys.max()), hy - int(ys.max())))
    out.sort(reverse=True)
    return int(sky.sum()), out


def main(path, *rest):
    bg = [int(v) for v in rest[:3]] if len(rest) >= 3 else list(SKY)
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    h, w, _ = a.shape
    hy = int(h * 0.556)
    print(f"== {path} ({w}x{h}) horizon y={hy}")
    for name, rgb in TAGS.items():
        m = (np.abs(a - np.array(rgb, np.int16)).sum(axis=2) < 40)
        npx, bl = blobs(m, hy)
        tag = "  <-- TARGET" if name.startswith("cable") else ""
        biggest = bl[0][0] if bl else 0
        print(f"  {name:30s} sky px {npx:6d}  blobs {len(bl):3d}  "
              f"largest {biggest:4d} px{tag}")
        if name.startswith("cable") or "telecom" in name:
            for nn, cw, ch, x0, y0, y1, gap in bl[:8]:
                print(f"      n={nn:5d} {cw:2d}x{ch:3d} at x{x0} y{y0}..{y1} ({gap}px above horizon)")


if __name__ == "__main__":
    main(sys.argv[1], *sys.argv[2:])
