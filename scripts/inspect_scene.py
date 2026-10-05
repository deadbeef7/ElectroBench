"""Scene inspector for scene 4: hunts for the pale-band road artifact and
prints cross-road / cross-shadow profiles used to verify the P7 fixes."""
import sys

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def lum(px, w, x, y):
    i = (y * w + x) * 3
    return (0.299 * px[i] + 0.587 * px[i+1] + 0.114 * px[i+2]) / 255.0

def med(v):
    v = sorted(v)
    return v[len(v)//2]

path = sys.argv[1]
w, h, px = load(path)
print("== %s  %dx%d ==" % (path, w, h))

# ---------------------------------------------------------------- band hunter
# A pale longitudinal band = a run of pixels much brighter than the surface
# they sit in, persisting down many rows at a consistent x. That is exactly
# what build P6's 35 cm gravel gap looked like.
y0, y1 = int(h*0.40), h-1
hits = {}
for y in range(y0, y1, 2):
    row = [lum(px, w, x, y) for x in range(w)]
    base = med(row)
    thr = base + 0.09
    x = 0
    while x < w:
        if row[x] > thr:
            s = x
            while x < w and row[x] > thr:
                x += 1
            if x - s >= 5:
                c = (s + x) // 2
                key = c // 12
                if key not in hits:
                    hits[key] = [0, 0.0, 0.0, 1e9, -1]
                e = hits[key]
                e[0] += 1
                e[1] += (x - s)
                e[2] = max(e[2], row[s:x][row[s:x].index(max(row[s:x]))])
                e[3] = min(e[3], c)
                e[4] = max(e[4], c)
        else:
            x += 1
print("  pale-band clusters (>=8 rows, width>=5px, >+0.09 vs row median):")
if not hits:
    print("    NONE")
for k in sorted(hits):
    e = hits[k]
    if e[0] >= 8:
        print("    x=%4d..%4d rows=%3d avgw=%5.1f peak=%.2f" %
              (e[3], e[4], e[0], e[1]/e[0], e[2]))

# --------------------------------------------------- cross-road luma profile
# The camera drives the road, so the wheel paths / centre seam / edge lines
# all show up as structure in a row that cuts across the carriageway.
print("  cross-road profiles (every 60 rows in the lower half):")
for y in range(int(h*0.55), h, 60):
    row = [lum(px, w, x, y) for x in range(w)]
    lo, hi, best = 1e9, -1, 0
    for i, v in enumerate(row):
        if v < lo:
            lo, best = v, i
    # dark run = carriageway
    x = best
    while x > 0 and row[x-1] < lo + 0.06:
        x -= 1
    x2 = best
    while x2 < w-1 and row[x2+1] < lo + 0.06:
        x2 += 1
    prof = []
    for x in range(max(0, x-14), min(w, x2+15), 2):
        prof.append("%.2f" % row[x])
    print("    row %3d road x=%4d..%4d : %s" % (y, x, x2, " ".join(prof)))
