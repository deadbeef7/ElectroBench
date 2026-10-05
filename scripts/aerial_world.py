"""Project every AddHouse TV aerial into the t=7 still and find the offender.

Reproduces AddHouse's roof maths and the scene's camera, so the aerial that
produced the flying mast can be named and the fix checked without guessing.
Prints the screen footprint of each aerial's mast and crossbars, and for the
one that lands on the artifact reports the roof height under its foot.
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
    xv, yv, zv = dot(s, d), dot(u, d), -dot(f, d)
    if zv <= 0.05:
        return None
    ft = 1.0 / math.tan(FOV * math.pi / 360.0)
    return (((ft / ASPECT) * xv / zv * 0.5 + 0.5) * W,
            (1.0 - (ft * yv / zv * 0.5 + 0.5)) * H)


def cam(t):
    z = 4.0 + (t * 2.6) % 161.0
    return ((2.0 + math.sin(t * 0.05) * 0.30,
             1.55 + 0.18 * math.sin(t * 0.11), z), (-0.45, 5.5, z + 26.0))


def houses():
    out = []
    for i in range(8):
        out.append(("L%d" % i, -11.5 - 2.0 * (i % 3), -4.0 + 19.0 * i + 3.0 * ((i * 7) % 3),
                    4.6 + 1.4 * ((i * 3) % 3), 5.2 + 1.2 * ((i * 5) % 3),
                    3.2 + 0.9 * ((i * 7) % 3), 1.0))
    for i in range(6):
        out.append(("R%d" % i, 15.0 + 2.5 * (i % 3), 8.0 + 21.0 * i + 2.5 * ((i * 5) % 3),
                    4.8 + 1.5 * ((i * 3) % 3), 5.4 + 1.1 * ((i * 7) % 3),
                    3.1 + 1.0 * ((i * 5) % 3), -1.0))
    return out


def main():
    t = float(sys.argv[1]) if len(sys.argv) > 1 else 7.0
    eye, look = cam(t)
    print("eye %s look %s" % (tuple(round(v, 3) for v in eye),
                              tuple(round(v, 3) for v in look)))
    print("\n%-4s %-22s %-18s %-18s %-18s" %
          ("id", "aerial OLD (x,y z,foot)", "screen OLD", "aerial NEW", "screen NEW"))
    for (tag, x, z, w, d, h, face) in houses():
        pick = int(math.fmod(abs(math.sin(x * 12.9898 + z * 78.233)) * 43758.5453, 5.0))
        rh = 0.55 + 0.06 * (pick % 3)
        yRidge = 0.10 + h + rh
        rows = []
        for k, (ax, az, foot) in enumerate((
                (x + w * 0.28, z - d * 0.22, -0.40),
                (x + w * 0.28, z - d * 0.06, -0.45))):
            pts = [(ax, yRidge + foot, az), (ax, yRidge + 0.95, az),
                   (ax - 0.30, yRidge + 0.62, az), (ax + 0.30, yRidge + 0.62, az),
                   (ax - 0.30, yRidge + 0.86, az), (ax + 0.30, yRidge + 0.86, az)]
            sp = [project(eye, look, p) for p in pts]
            if any(s is None for s in sp):
                rows.append(None)
                continue
            xs = [s[0] for s in sp]
            ys = [s[1] for s in sp]
            rows.append(((round(ax, 2), round(az, 2), yRidge + foot),
                         (round(min(xs)), round(max(xs)), round(min(ys)), round(max(ys)))))
        old, new = rows
        print("%-4s %-22s %-18s %-22s %-18s" % (
            tag,
            "%s y%.2f" % (old[0][:2], old[0][2]) if old else "-",
            "x%d-%d y%d-%d" % old[1] if old else "-",
            "%s y%.2f" % (new[0][:2], new[0][2]) if new else "-",
            "x%d-%d y%d-%d" % new[1] if new else "-"))


main()