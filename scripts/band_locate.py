"""Fast targeted probe: profile a specific x window over a specific row range
and map each column back to world coordinates on the ground plane."""
import sys, math

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

name = sys.argv[1]
xlo, xhi = int(sys.argv[2]), int(sys.argv[3])
ylo, yhi = int(sys.argv[4]), int(sys.argv[5])
W, H = 1600, 900
tsec = float(sys.argv[6]) if len(sys.argv) > 6 else 2.0
w, h, px = load('/tmp/insp/%s.ppm' % name)

def dot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a):
    l = math.sqrt(dot(a, a)); return (a[0]/l, a[1]/l, a[2]/l)

cz = 4.0 + (tsec*2.6) % 161.0
eye = (2.6 + math.sin(tsec*0.05)*0.35, 1.9 + 0.22*math.sin(tsec*0.11), cz)
fwd = norm((-0.45-eye[0], 5.5-eye[1], cz+26.0-eye[2]))
sAx = norm(cross(fwd, (0, 1, 0))); uAx = cross(sAx, fwd)
fcam = 1.0/math.tan(math.radians(52.0)/2)
asp = W/float(H)

def lum(x, y):
    i = (y*w+x)*3
    return (0.299*px[i]+0.587*px[i+1]+0.114*px[i+2])/255.0

def ground(x, y):
    ndcx, ndcy = 2.0*x/W - 1.0, 1.0 - 2.0*y/H
    vx, vy, vz = ndcx/(fcam/asp), ndcy/fcam, -1.0
    d = (sAx[0]*vx + uAx[0]*vy - fwd[0]*vz,
         sAx[1]*vx + uAx[1]*vy - fwd[1]*vz,
         sAx[2]*vx + uAx[2]*vy - fwd[2]*vz)
    if d[1] >= -1e-6:
        return None
    t = -eye[1]/d[1]
    return eye[0] + d[0]*t, eye[2] + d[2]*t, t

# find the row in the window with the widest bright run
best = (0, 0, 0, 0.0)
for y in range(ylo, yhi):
    run = 0; bestrun = (0, 0, 0.0)
    for x in range(xlo, xhi):
        if lum(x, y) > 0.80:
            if run == 0: s = x
            run += 1
            if run > bestrun[1]:
                bestrun = (s, run, lum(x, y))
        else:
            run = 0
    if bestrun[1] > best[1]:
        best = (y, bestrun[1], bestrun[0], bestrun[2])
print("== %s == widest >0.80 run in x[%d,%d]: row %d width %d at x=%d peak=%.2f"
      % (name, xlo, xhi, best[0], best[1], best[2], best[3]))
y = best[0]
print("  row %d profile (lum, world x, dist, tag):" % y)
for x in range(xlo, xhi, 6):
    g = ground(x, y)
    if not g:
        print("    px %4d  lum=%.2f   (sky)" % (x, lum(x, y))); continue
    gx, gz, dist = g
    tag = "CARRIAGEWAY" if -0.1 <= gx <= 5.3 else (
          "shoulder" if (5.3 < gx <= 6.3 or -1.1 <= gx < -0.1) else "verge")
    print("    px %4d  lum=%.2f  worldx=%6.2f  dist=%5.1f  %s"
          % (x, lum(x, y), gx, dist, tag))
