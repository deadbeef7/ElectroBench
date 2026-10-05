"""Sample the render at EXACT world coordinates by replicating the scene's
camera + projection in Python. Lets us confirm road features (wheel paths,
centre seam, edge lines) exist at the world x they are supposed to be at."""
import math, sys

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def mul(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def dot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0]/l, a[1]/l, a[2]/l)

def look_at(eye, ctr, up):
    f = norm(sub(ctr, eye)); s = norm(cross(f, up)); u = cross(s, f)
    return [s[0], s[1], s[2], -dot(s, eye),
            u[0], u[1], u[2], -dot(u, eye),
            -f[0], -f[1], -f[2], dot(f, eye),
            0.0, 0.0, 0.0, 1.0]

def persp(fovy, asp, zn, zf):
    f = 1.0/math.tan(math.radians(fovy)/2)
    return [f/asp,0,0,0, 0,f,0,0,
            0,0,(zf+zn)/(zn-zf),-1, 0,0,2*zf*zn/(zn-zf),0]

def matmul(A, B):   # column-major, C = A*B
    return [sum(A[k*4+r] * B[c*4+k] for k in range(4))
            for c in range(4) for r in range(4)]

def mv(m, v):       # column-major matrix * column vector
    return [sum(m[c*4+r]*v[c] for c in range(4)) for r in range(4)]

path, tsec, W, H = sys.argv[1], float(sys.argv[2]), 1600, 900
w, h, px = load(path)

cz = 4.0 + (tsec*2.6) % 161.0
eye = (2.6 + math.sin(tsec*0.05)*0.35,
       1.9 + 0.22*math.sin(tsec*0.11),
       cz)
ctr = (-0.45, 5.5, cz + 26.0)
V = look_at(eye, ctr, (0, 1, 0))
P = persp(52.0, W/float(H), 0.1, 900.0)
VP = matmul(P, V)

def project(p):
    o = mv(VP, (p[0], p[1], p[2], 1.0))
    if o[3] <= 0: return None
    ndc = (o[0]/o[3], o[1]/o[3])
    return ((ndc[0]*0.5+0.5)*W, (1.0 - (ndc[1]*0.5+0.5))*H)

def sample(x, y, r=2):
    tot = [0, 0, 0]; n = 0
    for dy in range(-r, r+1):
        for dx in range(-r, r+1):
            xi, yi = int(x)+dx, int(y)+dy
            if 0 <= xi < w and 0 <= yi < h:
                i = (yi*w+xi)*3
                tot[0] += px[i]; tot[1] += px[i+1]; tot[2] += px[i+2]; n += 1
    return tuple(c/n for c in tot)

print("camera eye=(%.2f,%.2f,%.2f)  t=%.1f" % (eye[0], eye[1], eye[2], tsec))
print("luma vs WORLD x across the road (road spans x=-0.10..5.30):")
for dz in (4.0, 8.0, 16.0, 40.0):
    out = []
    for wx10 in range(-15, 62, 2):
        wx = wx10/10.0
        s = project((wx, 0.030, cz + dz))
        if not s or not (0 <= s[0] < W and 0 <= s[1] < H):
            out.append("  -- ")
            continue
        r, g, b = sample(s[0], s[1])
        lum = (0.299*r + 0.587*g + 0.114*b)/255.0
        out.append("%4.2f" % lum)
    print("  dz=%4.0fm x=-1.5..6.1: %s" % (dz, " ".join(out)))
print()
print("  x grid: " + " ".join("%4.1f" % (x/10.0) for x in range(-15, 62, 2)))
print("  expect: shoulder<-0.1 | road -0.1..5.3 | wheelpaths 1.55 & 3.65 | seam 2.6")
