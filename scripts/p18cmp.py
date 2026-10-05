#!/usr/bin/env python3
"""Compare two scene-4 PPM renders.

The two things P18 had to move are:
  * the pole shafts, which must stop standing out against the sky, and
  * the cable web, which must start reading.

Shading-level masks cannot separate these from the sky (they sit within 30-80
luma of it), so this script does the honest thing: it reports the CHANGE
between two frames plus the local sky value beside each changed column, and
leaves the class attribution to the material tag pass.
"""
import sys


def read_ppm(path):
    with open(path, "rb") as fh:
        data = fh.read()
    # P6 header: magic, width, height, maxval -- whitespace separated
    parts = []
    i = 0
    while len(parts) < 4:
        while data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while data[i : i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[i : i + w * h * 3]


def luma(buf, w, x, y):
    o = (y * w + x) * 3
    return 0.2126 * buf[o] + 0.7152 * buf[o + 1] + 0.0722 * buf[o + 2]


def main():
    wa, ha, a = read_ppm(sys.argv[1])
    wb, hb, b = read_ppm(sys.argv[2])
    if (wa, ha) != (wb, hb):
        raise SystemExit("size mismatch %sx%s vs %sx%s" % (wa, ha, wb, hb))
    n = wa * ha
    changed = 0
    worse = 0
    better = 0
    sum_a = sum_b = 0.0
    for p in range(n):
        o = p * 3
        d = (
            abs(a[o] - b[o])
            + abs(a[o + 1] - b[o + 1])
            + abs(a[o + 2] - b[o + 2])
        )
        if d:
            changed += 1
            la = 0.2126 * a[o] + 0.7152 * a[o + 1] + 0.0722 * a[o + 2]
            lb = 0.2126 * b[o] + 0.7152 * b[o + 1] + 0.0722 * b[o + 2]
            sum_a += la
            sum_b += lb
            if lb < la:
                better += 1
            elif lb > la:
                worse += 1
    print("frame %dx%d  changed %d px (%.2f%%)" % (wa, ha, changed, 100.0 * changed / n))
    if changed:
        print("  mean luma of changed px: %.1f -> %.1f (%.2f)"
              % (sum_a / changed, sum_b / changed, sum_b / changed - sum_a / changed))
        print("  got darker %d   got brighter %d" % (better, worse))
    # global histogram shift, cheap sanity check on the grade
    for name, buf in (("old", a), ("new", b)):
        tot = 0.0
        dark = 0
        for p in range(0, n, 37):  # stride sample
            o = p * 3
            l = 0.2126 * buf[o] + 0.7152 * buf[o + 1] + 0.0722 * buf[o + 2]
            tot += l
            if l < 110:
                dark += 1
        k = len(range(0, n, 37))
        print("  %s: mean luma %.1f, px<110 %.2f%%" % (name, tot / k, 100.0 * dark / k))


main()
