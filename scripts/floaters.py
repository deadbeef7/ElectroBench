"""Find ISOLATED compact blobs in a render: floating geometry.

Anything that hangs in mid-air reads as a small connected component that is
detached from the poles / wires / ground. Wires are long and thin, so they are
filtered out by aspect ratio; a stray cylinder, box or stud shows up as a
compact component of roughly circular extent.
"""
import sys
from collections import deque


def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def sky_ref(px, w, h, y0, y1, x0, x1):
    """Median sky colour sampled from a clean patch of the upper frame."""
    s = []
    for y in range(y0, y1, 3):
        for x in range(x0, x1, 3):
            i = (y * w + x) * 3
            s.append((px[i], px[i + 1], px[i + 2]))
    s.sort()
    return s[len(s) // 2]


for path in sys.argv[1:]:
    w, h, px = load(path)
    ref = sky_ref(px, w, h, 4, h // 8, 4, w - 4)
    # "not sky" mask: differs from the reference sky by a visible margin
    mask = bytearray(w * h)
    for y in range(h):
        row = y * w
        for x in range(w):
            i = (row + x) * 3
            d = (abs(px[i] - ref[0]) + abs(px[i + 1] - ref[1]) +
                 abs(px[i + 2] - ref[2]))
            if d > 60:
                mask[row + x] = 1
    seen = bytearray(w * h)
    blobs = []
    for y0 in range(0, int(h * 0.72)):
        for x0 in range(w):
            if not mask[y0 * w + x0] or seen[y0 * w + x0]:
                continue
            q = deque([(x0, y0)])
            seen[y0 * w + x0] = 1
            pts = []
            while q:
                x, y = q.popleft()
                pts.append((x, y))
                if len(pts) > 4000:
                    break
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1),
                               (x, y - 1), (x + 1, y + 1), (x - 1, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h:
                        k = ny * w + nx
                        if mask[k] and not seen[k]:
                            seen[k] = 1
                            q.append((nx, ny))
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            bw = max(xs) - min(xs) + 1
            bh = max(ys) - min(ys) + 1
            blobs.append((len(pts), bw, bh, min(xs), min(ys), max(xs), max(ys)))
    print("== %s (%dx%d) sky_ref=%s == %d components" %
          (path, w, h, ref, len(blobs)))
    # compact + small + detached = floating cylinder candidate
    cands = [b for b in blobs
             if b[0] >= 6 and b[1] <= 34 and b[2] <= 34 and
             (b[1] / max(b[2], 1) > 0.45) and (b[2] / max(b[1], 1) > 0.45)]
    cands.sort(key=lambda b: -b[0])
    print("  compact isolated blobs (possible floaters): %d" % len(cands))
    for n, bw, bh, x0, y0, x1, y1 in cands[:25]:
        i = ((y0 + bh // 2) * w + x0 + bw // 2) * 3
        print("    area=%5d bbox=%dx%d at (%d,%d)-(%d,%d) centre RGB=%d,%d,%d" %
              (n, bw, bh, x0, y0, x1, y1, px[i], px[i + 1], px[i + 2]))