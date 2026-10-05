"""Per-class sky fragments from the material-tag render, ranked by 'cylinder-ness'.

For each tagged material class: find connected components that sit in the sky,
report the bounding box, the aspect (a flying cylinder is TALL and NARROW),
and the class immediately beneath the component's lowest pixels. A component
whose underside is SKY and whose width is <= 4 px is the shape the complaint is
about; one whose underside is roof/wall/concrete is standing on something.
"""
import sys
import numpy as np
from PIL import Image
from collections import deque

# tag colour -> name (must match the TEMP DIAGNOSTIC block in object_frag.glsl)
TAGS = {
    (255, 0, 255): "CABLE",
    (0, 255, 255): "CERAMIC",
    (0, 255, 0): "METAL",
    (255, 0, 0): "WALL",
    (255, 255, 0): "SHADOW/GLOW",
    (255, 128, 0): "STEEL",
    (128, 0, 255): "KERB",
    (0, 128, 255): "CONCRETE",
    (64, 64, 64): "OTHER-OBJ",
}


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def classify(img):
    q = img // 48
    out = np.full(q.shape[:2], -1, dtype=np.int32)   # -1 = sky
    for i, (c, name) in enumerate(TAGS.items()):
        out[(q[:, :, 0] == c[0] // 48) & (q[:, :, 1] == c[1] // 48) &
            (q[:, :, 2] == c[2] // 48)] = i
    return out


def main():
    p = sys.argv[1]
    skybot = int(sys.argv[2]) if len(sys.argv) > 2 else 360
    cls = classify(load(p).astype(np.int32))
    names = [n for _, n in TAGS.items()]
    h, w = cls.shape
    print("%s  %dx%d  sky below y=%d" % (p, w, h, skybot))
    for i, name in enumerate(names):
        mask = cls == i
        n = int(mask.sum())
        if n == 0:
            print("  %-11s ABSENT" % name)
            continue
        sky = cls == -1
        out = []
        seen = np.zeros_like(mask)
        ys, xs = np.nonzero(mask & (np.arange(h)[:, None] < skybot))
        for y0, x0 in zip(ys, xs):
            if seen[y0, x0]:
                continue
            q = deque([(y0, x0)])
            seen[y0, x0] = True
            pts = []
            while q:
                y, x = q.popleft()
                pts.append((y, x))
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        q.append((ny, nx))
            pa = np.array(pts)
            bx0, bx1 = int(pa[:, 1].min()), int(pa[:, 1].max())
            by0, by1 = int(pa[:, 0].min()), int(pa[:, 0].max())
            wd, ht = bx1 - bx0 + 1, by1 - by0 + 1
            if len(pts) < 4 or ht < 8:
                continue
            # what is under the lowest pixels?
            under = {}
            for (y, x) in pa:
                if y + 1 < h:
                    k = int(cls[y + 1, x])
                    under[k] = under.get(k, 0) + 1
            skyunder = under.get(-1, 0)
            out.append((wd, ht, len(pts), bx0, bx1, by0, by1,
                        skyunder, max(under.items(), key=lambda kv: kv[1])))
        out.sort(key=lambda r: -(r[2]))
        fly = [r for r in out if r[7] > r[2] * 0.5]
        print("  %-11s %6d px  %4d sky fragments, %d sky-backed"
              % (name, n, len(out), len(fly)))
        for (wd, ht, px, bx0, bx1, by0, by1, su, top) in fly[:12]:
            print("      x%4d-%4d (%2dw) y%4d-%4d (%3dh) %5dpx  sky-under %d/%d (%.0f%%)  tallest-h/w %.1f  under=%s"
                  % (bx0, bx1, wd, by0, by1, ht, px, su, px, 100.0 * su / px,
                     ht / float(wd), names[top[0]] if top[0] >= 0 else "SKY"))


main()