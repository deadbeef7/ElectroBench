"""Are the remaining isolated bright dots real glitter, or aliasing?

P23 measured "pixellated" as the share of water pixels standing >0.06 above
their own 15x15 background, and got the water from 24.97% to 17.44%. That
number is still ~18% after P24. Whether 18% is a DEFECT depends on what the
dots are:

  a real sun glitter path IS made of isolated specular sparkles, so some
  share is expected and correct;
  what is NOT correct is every dot being exactly ONE pixel. A sparkle the eye
  should read as a point of light has a size set by the pixel footprint. When
  the whole population is 1 px and nothing is 2-3 px, the surface is being
  sampled faster than it is resolved -- that is aliasing, and it is grain.

So: label the bright dots, measure the connected-component size histogram, and
report the share that are singletons. Isolated components of size 1 dominate =
grain. A tail of size 2-4 = resolved glitter.

Usage: seaspots.py <frame.ppm> [more frames...]
"""
import sys
import numpy as np


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def horizon(lum):
    col = lum.mean(axis=1)
    cs = np.convolve(col, np.ones(5) / 5.0, mode="same")
    d = np.diff(cs[: int(lum.shape[0] * 0.70)])
    return int(np.argmin(d)) + 1


def boxmean(x, bg=15):
    pad = bg // 2
    p = np.pad(x, pad, mode="edge")
    cs = np.cumsum(np.cumsum(p, axis=0), axis=1)
    cs = np.pad(cs, ((1, 0), (1, 0)))
    h, w = x.shape
    ii, jj = np.meshgrid(np.arange(h), np.arange(w), indexing="ij")
    return ((cs[ii + bg, jj + bg] - cs[ii, jj + bg] -
             cs[ii + bg, jj] + cs[ii, jj]) / (bg * bg))


def label(mask):
    """4-connected labelling by iterative flood; small frames, no deps."""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    cur = 0
    idx = np.argwhere(mask)
    seen = np.zeros((h, w), bool)
    for y0, x0 in idx:
        if seen[y0, x0]:
            continue
        cur += 1
        stack = [(y0, x0)]
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            lab[y, x] = cur
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
    return lab, cur


def main():
    allhist = {}
    for path in sys.argv[1:]:
        a = load(path)
        lum = a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        hy = horizon(lum)
        water = lum[hy:, :]
        mask = water > boxmean(water) + 0.06
        lab, n = label(mask)
        sizes = np.bincount(lab.ravel())[1:]
        h1 = int((sizes == 1).sum())
        print("%s  horizon~y%d  bright px %6d  components %6d  singleton %5.1f%%"
              % (path.split("/")[-1], hy, int(mask.sum()), n,
                 100.0 * h1 / max(n, 1)))
        for s in sizes:
            allhist[s] = allhist.get(s, 0) + 1
    tot = sum(allhist.values())
    print("\ncomponent size distribution (all frames pooled)")
    for s in sorted(allhist)[:8]:
        print("  size %d: %6d  %5.1f%%" % (s, allhist[s], 100.0 * allhist[s] / tot))
    single = allhist.get(1, 0)
    multi = tot - single
    print("  singletons %5.1f%%   multi-pixel %5.1f%%"
          % (100.0 * single / tot, 100.0 * multi / tot))


main()