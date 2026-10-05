"""Final P8 verification: tonal comparison + detection of the new emissive
geometry (a lit vending machine / lamp lens is the only COOL light in an
otherwise warm dusk frame, so pixels with B >= R are a clean signature)."""
import sys

def load(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

for path in sys.argv[1:]:
    w, h, px = load(path)
    cool = coolbright = black = 0
    buckets = {'sky': [0, 0, 0], 'mid': [0, 0, 0], 'ground': [0, 0, 0]}
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            i = (y*w+x)*3
            r, g, b = px[i], px[i+1], px[i+2]
            if r == 0 and g == 0 and b == 0:
                black += 1
            if b >= r and b >= 120:
                cool += 1
                if b >= 200:
                    coolbright += 1
            k = 'sky' if y < h*0.45 else ('mid' if y < h*0.62 else 'ground')
            buckets[k][0] += r; buckets[k][1] += g; buckets[k][2] += b
    n = (h//2+1)*(w//2+1)
    print("== %s (%dx%d) ==" % (path, w, h))
    print("   pure black            : %5.2f%%" % (100.0*black/n))
    print("   cool emissive px (B>=R, B>=120) : %5.3f%%  bright(B>=200): %5.3f%%"
          % (100.0*cool/n, 100.0*coolbright/n))
    for k in ('sky', 'mid', 'ground'):
        r, g, b = buckets[k]
        c = max(1, sum(1 for _ in (0,)))
        print("   %-7s mean RGB = %5.1f/%5.1f/%5.1f" % (k, r/ (n*0.28), g/(n*0.28), b/(n*0.28)))
