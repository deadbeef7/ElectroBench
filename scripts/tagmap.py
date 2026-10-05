"""Read the material-tag render: is each metal fragment's foot on ROOF?

Tags in the pass: green = kMatMetal, red = kMatRoof, blue = kMatWall,
magenta = kMatCable, cyan = kMatCeramic, grey = everything else. Sky keeps its
own shader, so sky is whatever is NOT one of those six. For every green
component it reports the class directly beneath its lowest pixel: roof means
anchored, sky means floating.
"""
import sys
import numpy as np
from PIL import Image

CLASSES = {
    (0, 255, 0): "METAL",
    (255, 0, 0): "ROOF",
    (0, 0, 255): "WALL",
    (255, 0, 255): "CABLE",
    (0, 255, 255): "CERAMIC",
}


def classify(img):
    q = (img // 60).astype(np.int32)
    out = np.full(q.shape[:2], 6, dtype=np.int32)  # 6 = sky/other background
    for (r, g, b), i in zip(CLASSES, range(5)):
        out[(q[:, :, 0] == r // 60) & (q[:, :, 1] == g // 60) &
            (q[:, :, 2] == b // 60)] = i
    out[(q[:, :, 0] == 0) & (q[:, :, 1] == 0) & (q[:, :, 2] == 0)] = 5  # other object
    return out


def main():
    img = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
    cls = classify(img)
    print("class pixel counts:",
          {n: int((cls == i).sum()) for i, n in
           enumerate(["METAL", "ROOF", "WALL", "CABLE", "CERAMIC", "GREY"])},
          "SKY", int((cls == 6).sum()))
    h, w = cls.shape
    metal = cls == 0
    seen = np.zeros_like(metal)
    comps = []
    from collections import deque
    ys, xs = np.nonzero(metal)
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
                if 0 <= ny < h and 0 <= nx < w and metal[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        pa = np.array(pts)
        comps.append((len(pts), pa[:, 1].min(), pa[:, 1].max(),
                      pa[:, 0].min(), pa[:, 0].max(), pa))
    comps.sort(reverse=True, key=lambda c: c[0])
    print("\nmetal components above the horizon, with the class under the foot:")
    shown = 0
    for (n, x0, x1, y0, y1, pa) in comps:
        tall = y1 - y0 + 1
        if tall < 12 or y0 > 440:
            continue
        under = {}
        for (y, x) in pa:
            for dy in (1, 2, 3):
                yy, xx = y + dy, x
                if 0 <= yy < h and 0 <= xx < w:
                    k = int(cls[yy, xx])
                    under[k] = under.get(k, 0) + 1
        names = {0: "METAL", 1: "ROOF", 2: "WALL", 3: "CABLE", 4: "CERAMIC",
                 5: "OTHER", 6: "SKY"}
        top = sorted(under.items(), key=lambda kv: -kv[1])[:3]
        tag = " ".join("%s=%d" % (names[k], v) for k, v in top)
        print("   x%4d-%4d (%2d w)  y%4d-%4d (%3d tall) %5d px   under: %s"
              % (x0, x1, x1 - x0 + 1, y0, y1, tall, n, tag))
        shown += 1
        if shown >= 26:
            break


main()