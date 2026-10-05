"""Vertical + horizontal pixel probe: locate the black region boundary exactly."""
import sys

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

w, h, px = load(sys.argv[1])
xs = [int(w*0.35), int(w*0.5), int(w*0.65)]

def rgb(x, y):
    i = (y*w+x)*3
    return px[i], px[i+1], px[i+2]

print("vertical scan (RGB every 12 rows, y=520..899):")
for y in range(520, h, 12):
    out = []
    for x in xs:
        r, g, b = rgb(x, y)
        out.append("(%4d,%4d,%4d)" % (r, g, b))
    print("  y=%3d  %s" % (y, "  ".join(out)))

print("horizontal scan at y=800, every 40 px:")
y = 800
out = []
for x in range(0, w, 40):
    r, g, b = rgb(x, y)
    out.append("%d:(%d,%d,%d)" % (x, r, g, b))
print("  " + " ".join(out))

# fraction of exactly-black pixels per row band
print("black fraction by row band:")
for y0 in range(0, h, 50):
    n = b = 0
    for y in range(y0, min(y0+50, h), 2):
        for x in range(0, w, 3):
            n += 1
            r, g, bl = rgb(x, y)
            if r == 0 and g == 0 and bl == 0:
                b += 1
    print("  y=%3d-%3d : %5.1f%%" % (y0, min(y0+50, h), 100.0*b/max(n, 1)))
