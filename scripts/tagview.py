"""Print the material-tag render as a class map, one char per pixel-pair.

Flat saturated tags have maximum contrast, so unlike the shaded frame this is
actually readable as text: cable = C, metal = M, steel = S, ceramic = c, wall/roof
= W, kerb = K, concrete = N, other = o, sky = space.
"""
import sys
import numpy as np

TAGS = {
    (255, 0, 255): "C",   # cable
    (0, 255, 255): "c",   # ceramic
    (0, 255, 0): "M",     # metal
    (255, 0, 0): "W",     # wall / roof / glass / leaf
    (255, 255, 0): "G",   # shadow / glow
    (255, 128, 0): "S",   # steel
    (128, 0, 255): "K",   # kerb
    (0, 128, 255): "N",   # concrete
    (64, 64, 64): "o",    # other object
}


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def main():
    p = sys.argv[1]
    x0, x1, y0, y1 = (int(v) for v in sys.argv[2:6])
    step = int(sys.argv[6]) if len(sys.argv) > 6 else 2
    img = load(p)
    q = img // 48
    cls = np.full(q.shape[:2], " ", dtype="<U1")
    for c, ch in TAGS.items():
        m = ((q[:, :, 0] == c[0] // 48) & (q[:, :, 1] == c[1] // 48) &
             (q[:, :, 2] == c[2] // 48))
        cls[m] = ch
    print("%s x%d-%d y%d-%d  (%d cols, 1 char = %d px wide)"
          % (p, x0, x1, y0, y1, (x1 - x0) // step, step))
    print("     " + "".join(str((x0 + i * step) // 10 % 10)
                            for i in range((x1 - x0) // step)))
    print("     " + "".join(str((x0 + i * step) % 10)
                            for i in range((x1 - x0) // step)))
    for y in range(y0, y1):
        row = "".join(cls[y, x0 + i * step] for i in range((x1 - x0) // step))
        print("%4d %s" % (y, row))


main()