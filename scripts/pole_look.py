import sys

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    return w, h, px

def luma(w, h, px, x, y):
    i = (y * w + x) * 3
    return (0.299 * px[i] + 0.587 * px[i+1] + 0.114 * px[i+2]) / 255.0

for path in sys.argv[1:]:
    w, h, px = load(path)
    print("== %s  %dx%d ==" % (path, w, h))
    # structure coverage: dark pixels (luma<160/255) in the lower 2/3
    tot = 0; dark = 0
    for y in range(h//3, h):
        for x in range(0, w, 2):
            tot += 1
            if luma(w, h, px, x, y) < 160/255.0: dark += 1
    print("  structure(lower2/3, luma<160) = %.1f%%" % (100.0*dark/tot))
    # ascii map
    CW, CH = 104, 34
    ramp = " .:-=+*#%@"
    for r in range(CH):
        line = []
        for c in range(CW):
            x0, x1 = c*w//CW, (c+1)*w//CW
            y0, y1 = r*h//CH, (r+1)*h//CH
            s = n = 0
            for y in range(y0, max(y0+1, y1), 2):
                for x in range(x0, max(x0+1, x1), 2):
                    s += luma(w, h, px, x, y); n += 1
            v = s/max(n, 1)
            line.append(ramp[min(9, int(v*9.999))])
        print("  |" + "".join(line) + "|")
    # skyline: topmost dark pixel per column over the sky band. Wires and
    # pole arms show up as isolated marks; trees/houses as solid blocks.
    print("  --- skyline (topmost luma<0.46, 'o' = also more dark below) ---")
    CH2 = 26
    for c in range(CW):
        col = []
        runs = 0
        inside = False
        firsty = -1
        for r in range(CH2):
            y0, y1 = r*h//CH2, (r+1)*h//CH2
            x0, x1 = c*w//CW, (c+1)*w//CW
            dark = 0; n = 0
            for y in range(y0, max(y0+1, y1)):
                for x in range(x0, max(x0+1, x1)):
                    n += 1
                    if luma(w, h, px, x, y) < 0.46: dark += 1
            d = dark*3 > n
            if d and firsty < 0: firsty = r
            if d and not inside: runs += 1
            inside = d
            col.append('#' if d else ' ')
        top = firsty if firsty >= 0 else CH2-1
        print("%3d r=%2d k=%d |%s|" % (c, top, runs, "".join(col)))
