"""Recover which sim time a committed still was shot at, from the camera maths.

The scene's dolly is deterministic, so the pole shafts in the old PNG can be
matched against projections computed for a sweep of t. Picked the wrong t and
you get a frame with a different camera position and no comparison is possible.
"""
import math
import sys
import numpy as np
from PIL import Image

W, H = 1600, 900
FOV = 52.0
ASPECT = W / H


def norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def project(eye, look, p):
    f = norm(sub(look, eye))
    s = norm(cross(f, (0.0, 1.0, 0.0)))
    u = cross(s, f)
    d = sub(p, eye)
    xv = dot(s, d)
    yv = dot(u, d)
    zv = -dot(f, d)
    if zv <= 0.05:
        return None
    ft = 1.0 / math.tan(FOV * math.pi / 360.0)
    ndcx = (ft / ASPECT) * xv / zv
    ndcy = ft * yv / zv
    return ((ndcx * 0.5 + 0.5) * W, (1.0 - (ndcy * 0.5 + 0.5)) * H)


def cam(t):
    span = 161.0
    z = 4.0 + (t * 2.6) % span
    return (2.0 + math.sin(t * 0.05) * 0.30,
            1.55 + 0.18 * math.sin(t * 0.11),
            z), (-0.45, 5.5, z + 26.0)


def poles():
    out = []
    for i in range(15):
        out.append((-3.4, 8.6 + 0.35 * ((i * 5) % 3), 2.0 + 12.4 * i,
                    0.10 * ((i * 7) % 3 - 1), 0.08 * ((i * 5) % 3 - 1),
                    8.6 + 0.35 * ((i * 5) % 3)))
    return out


def main():
    img = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.float64)
    lum = img @ np.array([0.299, 0.587, 0.114])
    best = []
    for step in range(0, 361):
        t = step * 0.05
        eye, look = cam(t)
        score, n = 0.0, 0
        for (px, hgt, pz, lx, lz, ph) in poles():
            # just below the main crossarm: a shaft pixel against open sky
            f = (hgt - 0.55 - 2.2) / ph
            p = (px + lx * f, hgt - 0.55 - 2.2, pz + lz * f)
            s = project(eye, look, p)
            if s is None:
                continue
            x, y = s
            if not (6 < x < W - 6 and 6 < y < H - 6):
                continue
            xi, yi = int(x), int(y)
            band = lum[max(0, yi - 14):yi + 14, xi - 2:xi + 3]
            # sky is the brightest thing in the upper frame: a shaft is a dark dip
            score += float(band.mean())
            n += 1
        if n >= 3:
            best.append((score / n, t, n))
    best.sort()
    for s, t, n in best[:8]:
        print("t=%6.2f  mean luma at shaft samples %6.2f  (n=%d)" % (t, s, n))
    print("...worst:", ["%.2f@%.2f" % (s, t) for s, t, _ in best[-3:]])


main()