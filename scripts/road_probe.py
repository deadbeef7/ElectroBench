import sys
# horizontal scanline probe: prints luma across a row to spot bright strips
path = sys.argv[1]
row = int(float(sys.argv[2]))
d = open(path, 'rb').read()
parts = d.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
print("%s %dx%d row=%d" % (path, w, h, row))
vals = []
for x in range(w):
    i = (row * w + x) * 3
    l = (0.299 * px[i] + 0.587 * px[i+1] + 0.114 * px[i+2]) / 255.0
    vals.append(l)
# compress to 88 buckets, report min/max per bucket
N = 88
for b in range(N):
    seg = vals[b*w//N:(b+1)*w//N]
    mn = min(seg); mx = max(seg); av = sum(seg)/len(seg)
    bar = "#" * int(mx*20)
    print("%3d  av=%.2f [%5.2f-%5.2f] %s" % (b, av, mn, mx, bar))
