#!/usr/bin/env python3
"""BUILD-P26: one table, one metric set, every saved scene-2 render.

The P26 attempts were each judged with a slightly different threshold, which
is how a change could be reported as "-55% plates" in one run and "+28% plates"
in another. Everything below is computed identically for every image.

Two families of metric, because they answer different questions:

  EDGE family (phase-stable). The complaint is that the sea is *hard-edged*.
  Counting large Sobel responses is insensitive to WHICH phase the wave field
  happens to be in: there are always edges, so a change that removes a class
  of boundary shows up in the count and not in the noise.

  PLATE family (phase-noisy). "Pale flat islands with dark rims" is about
  *connected area*, and the plates are the sky cubemap mirrored through
  wave-driven normals, so their size is locked to the wave phase: a change
  measured at t=36 can invert at t=32 for no reason connected to the change.
  So this family is reported, but only ever read across all three phases.

  The plate test is MULTI-SCALE: a plate is flat at 25-40 px. Fine glitter is
  flat at neither scale -- it has full variance in a 9x9 window. A single-scale
  flatness test cannot tell them apart, which is how the earlier
  "block-step" metric fooled us.

Usage: p26report.py label=path [label=path ...]
"""
import os
import sys
import numpy as np
from PIL import Image

SOX = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], float)


def conv2(a, k):
    """2-D convolution, edge-padded."""
    h, w = a.shape
    p = np.pad(a, 1, mode='edge')
    out = np.zeros_like(a)
    for i in range(3):
        for j in range(3):
            out += k[i, j] * p[i:i + h, j:j + w]
    return out


def boxsum(a, K):
    """Sum over a KxK window via a padded 2-D cumulative sum."""
    p = np.pad(a, K // 2, mode='edge')
    cs = np.pad(np.cumsum(np.cumsum(p, axis=0), axis=1), ((1, 0), (1, 0)))
    sh, sw = a.shape
    yy, xx = np.mgrid[0:sh, 0:sw]
    return cs[yy + K, xx + K] - cs[yy, xx + K] - cs[yy + K, xx] + cs[yy, xx]


def local_std(a, K):
    s1 = boxsum(a, K) / float(K * K)
    s2 = boxsum(a * a, K) / float(K * K)
    return np.sqrt(np.maximum(s2 - s1 * s1, 0.0))


def max_blob(mask, block=4):
    """Largest connected blob area in px, via block-downsampled BFS.

    scipy is not available here, so this is a hand-rolled flood fill on a
    coarse grid: plate boundaries are far larger than `block`, so coarsening
    costs nothing and keeps the fill cheap on 1 CPU.
    """
    h, w = mask.shape
    hb, wb = h // block, w // block
    m = mask[:hb * block, :wb * block].reshape(hb, block, wb, block).mean(axis=(1, 3))
    solid = m > 0.5
    seen = np.zeros_like(solid, bool)
    best = 0
    for sy in range(hb):
        for sx in range(wb):
            if not solid[sy, sx] or seen[sy, sx]:
                continue
            stack = [(sy, sx)]
            seen[sy, sx] = True
            n = 0
            while stack:
                y, x = stack.pop()
                n += 1
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < hb and 0 <= nx < wb and solid[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            best = max(best, n)
    return best * block * block


def report(label, path):
    im = np.asarray(Image.open(path).convert('RGB')).astype(np.float32) / 255.0
    h, w, _ = im.shape
    lum = 0.2126 * im[:, :, 0] + 0.7152 * im[:, :, 1] + 0.0722 * im[:, :, 2]

    # sea = everything below the horizon.
    # NOT auto-detected. "Brightest row" lands in the bright low SKY (~380) and
    # a lagged-step detector wandered from 364 to 602 across these renders,
    # which is worse than useless when the whole point is comparing pixels.
    # Every 1280x720 scene-2 frame here shares camera geometry (radius ~52 m,
    # pitch ~0.050) and its row profile steps down at y=405->410, so the sea
    # band is pinned to a FIXED row and every image is measured on identical
    # pixels. Override with SEA_Y0=... when checking another resolution.
    y0 = int(os.environ.get('SEA_Y0', '412'))
    sea = lum[y0:, :]

    gx, gy = conv2(sea, SOX), conv2(sea, SOX.T)
    g = np.hypot(gx, gy)

    # left / centre split: the dusk sea is asymmetric about the sun column
    w3 = sea.shape[1] // 3
    lc = sea[:, :w3].mean() / max(sea[:, w3:2 * w3].mean(), 1e-4)

    # multi-scale plate test
    lstd_s = local_std(sea, 9)
    lstd_m = local_std(sea, 33)
    pale = sea > np.percentile(sea, 70)
    # a plate: locally flat at the LARGE scale and not glitter at the small one
    plate = pale & (lstd_m < 0.018) & (lstd_s < 0.055)

    # rim: pale pixel touching something much darker
    inner = sea[1:-1, 1:-1]
    darker = np.zeros_like(inner, bool)
    for sh in (np.roll(inner, 1, 0), np.roll(inner, -1, 0),
               np.roll(inner, 1, 1), np.roll(inner, -1, 1)):
        darker |= (sh < inner - 0.12)
    pale_in = inner > np.percentile(sea, 70)
    rim = (pale_in & darker).sum() / max(pale_in.sum(), 1)

    print(f'{label:>10} | y0 {y0:3d} | mean {sea.mean():.4f} | L/C {lc:5.2f} '
          f'| E>.045 {100*(g > .045).mean():5.2f}% | E>.08 {100*(g > .08).mean():5.2f}% '
          f'| E>.12 {100*(g > .12).mean():5.2f}% | gbar {g.mean():.4f} '
          f'| plate {100*plate.mean():5.2f}% | maxblob {max_blob(plate):5d} '
          f'| rim {100*rim:5.1f}%')


if __name__ == '__main__':
    hdr = f'{"run":>10} |     | sea   | sun   |  ---- edge density ---- | grad | --- plates --- |'
    print(hdr)
    for arg in sys.argv[1:]:
        label, path = arg.split('=', 1)
        report(label, path)