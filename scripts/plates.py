#!/usr/bin/env python3
"""Quantify the 'hard-edged pale plates' signature on the sea.

The complaint is SPATIAL (camo/leopard islands with sharp boundaries), so it
must be measured with spatial statistics, not a mean/std over the sun column:

  * edge density    - fraction of sea pixels whose Sobel magnitude is large,
                      i.e. how much of the sea is boundary
  * edge sharpness  - mean |grad| over the top 2% of edges. A soft gradient
                      has a long tail of small gradients; a hard threshold has
                      a few very large ones.
  * plateau fraction- fraction of sea pixels sitting in a locally flat patch
                      (low local std over a 9x9 window) that is also pale.
                      This is the 'flat light plate' half of the signature.
  * rim fraction    - of the pale pixels, how many are adjacent to a much
                      darker pixel. 'Dark-rimmed islands' is the other half.
  * bimodality      - the sea luma histogram's dip between the dark and pale
                      modes. Islands imply two populations with a gap.

Usage: plates.py FILE [y0 y1]   (y0,y1 default to the sea below the horizon)
"""
import sys
import numpy as np
from PIL import Image


def sobel(g):
    kx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], float)
    ky = kx.T
    h, w = g.shape
    p = np.pad(g, 1, mode='edge')
    out = np.zeros_like(g)
    for i in range(3):
        for j in range(3):
            out += kx[i, j] * p[i:i + h, j:j + w]
    p = np.pad(g, 1, mode='edge')
    gx = np.zeros_like(g)
    for i in range(3):
        for j in range(3):
            gx += kx[i, j] * p[i:i + h, j:j + w]
    p = np.pad(g, 1, mode='edge')
    gy = np.zeros_like(g)
    for i in range(3):
        for j in range(3):
            gy += ky[i, j] * p[i:i + h, j:j + w]
    return np.hypot(gx, gy)


def report(path, y0=None, y1=None):
    im = np.asarray(Image.open(path).convert('RGB')).astype(np.float32) / 255.0
    h, w, _ = im.shape
    lum = 0.2126 * im[:, :, 0] + 0.7152 * im[:, :, 1] + 0.0722 * im[:, :, 2]
    if y0 is None or y1 is None:
        # find the horizon: brightest row from the top, sea starts below it
        col = lum.mean(axis=1)
        y0 = int(np.argmax(col)) + 12
        y1 = h
    sea = lum[y0:y1, :]
    g = sobel(sea)

    edge_thr = 0.045
    edges = g > edge_thr
    top = g[edges] if edges.any() else np.array([0.0])
    k = max(1, int(len(top) * 0.02))
    sharp = np.sort(top)[-k:].mean()

    # local flatness over a 9x9 window (separable box via cumulative sums)
    K = 9
    pad = np.pad(sea, K // 2, mode='edge')
    cs = np.pad(np.cumsum(np.cumsum(pad, axis=0), axis=1), ((1, 0), (1, 0)))

    def box(y, x):
        return cs[y + K, x + K] - cs[y, x + K] - cs[y + K, x] + cs[y, x]

    sh, sw = sea.shape
    yy, xx = np.mgrid[0:sh, 0:sw]
    mean = box(yy, xx) / float(K * K)
    mean2 = box(yy, xx)  # same window of the square -> use second cumsum pass
    cs2 = np.pad(np.cumsum(np.cumsum(pad * pad, axis=0), axis=1),
                 ((1, 0), (1, 0)))

    def box2(y, x):
        return cs2[y + K, x + K] - cs2[y, x + K] - cs2[y + K, x] + cs2[y, x]

    var = np.maximum(box2(yy, xx) / float(K * K) - mean * mean, 0.0)
    lstd = np.sqrt(var)
    flat = lstd < 0.030
    pale = sea > np.percentile(sea, 70)
    plate = flat & pale

    # rim: pale pixel with a much darker 4-neighbour
    inner = sea[1:-1, 1:-1]
    darker = np.zeros_like(inner, bool)
    for sh, ax in ((np.roll(inner, 1, 0), 0), (np.roll(inner, -1, 0), 0),
                   (np.roll(inner, 1, 1), 1), (np.roll(inner, -1, 1), 1)):
        darker |= (sh < inner - 0.12)
    pale_in = inner > np.percentile(sea, 70)
    rim = pale_in & darker

    # bimodality: 40-bin histogram of the sea, deepest bin between the modes
    hist, edges_ = np.histogram(sea.ravel(), bins=40, range=(0.0, 1.0))
    hist = hist / hist.sum()
    dark_modes = [i for i in range(40) if 0.08 <= (i + .5) / 40 <= 0.40]
    pale_modes = [i for i in range(40) if 0.55 <= (i + .5) / 40 <= 0.95]
    dip = 1.0
    if dark_modes and pale_modes:
        a, b = dark_modes[-1], pale_modes[0]
        if b > a + 1:
            dip = 1.0 - hist[a + 1:b].min() / max(hist[a], hist[b])

    print(f'--- {path}  sea rows {y0}..{y1} ---')
    print(f'  sea mean luma        {sea.mean():.4f}')
    print(f'  edge density (>0.045){edges.mean() * 100:6.2f} %')
    print(f'  edge sharpness (p2)  {sharp:.4f}')
    print(f'  flat+pale plates     {plate.mean() * 100:6.2f} %')
    print(f'  pale w/ dark rim     {rim.sum() / max(pale_in.sum(), 1) * 100:6.2f} % of pale')
    print(f'  hist dip (bimodal)   {dip * 100:6.2f} %')
    return dict(edge=edges.mean(), sharp=sharp, plate=plate.mean(), rim=rim.mean(), dip=dip)


if __name__ == '__main__':
    f = sys.argv[1]
    y0 = int(sys.argv[2]) if len(sys.argv) > 2 else None
    y1 = int(sys.argv[3]) if len(sys.argv) > 3 else None
    report(f, y0, y1)
