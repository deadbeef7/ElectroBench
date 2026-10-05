#!/usr/bin/env python3
"""Where in the sea do the hard-edged pale plates actually live?

Reflection share is not constant: fresnel runs ~0.11 looking down at water
15 m away and ~0.92 at grazing incidence, so the far field is almost pure
sky and the near field is almost pure water body. Picking a fix means knowing
which field the complaint is in, so band the sea by screen row (rows map
monotonically to distance at a fixed camera) and report both statistics and
the two terms' likely shares.

Usage: bands.py IMAGE [IMAGE ...]
"""
import os
import sys
import numpy as np
from PIL import Image

SOX = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], float)


def conv2(a, k):
    h, w = a.shape
    p = np.pad(a, 1, mode='edge')
    out = np.zeros_like(a)
    for i in range(3):
        for j in range(3):
            out += k[i, j] * p[i:i + h, j:j + w]
    return out


def main(path):
    im = np.asarray(Image.open(path).convert('RGB')).astype(np.float32) / 255.0
    lum = 0.2126 * im[:, :, 0] + 0.7152 * im[:, :, 1] + 0.0722 * im[:, :, 2]
    y0 = int(os.environ.get('SEA_Y0', '412'))
    sea = lum[y0:, :]
    h = sea.shape[0]
    # grazing angle -> fresnel share, as the shader's Schlick chain gives it
    print(f'== {path}  sea rows {y0}..{y0 + h} ==')
    print(f'{"band":>10} | mean | E>.08 | rim% | pale% | est.fresnel')
    n = 6
    for b in range(n):
        r0, r1 = b * h // n, (b + 1) * h // n
        band = sea[r0:r1, :]
        g = np.hypot(conv2(band, SOX), conv2(band, SOX.T))
        inner = band[1:-1, 1:-1]
        darker = np.zeros_like(inner, bool)
        for sh in (np.roll(inner, 1, 0), np.roll(inner, -1, 0),
                   np.roll(inner, 1, 1), np.roll(inner, -1, 1)):
            darker |= (sh < inner - 0.12)
        pale = band > np.percentile(sea, 70)
        pin = inner > np.percentile(sea, 70)
        rim = 100.0 * (pin & darker).sum() / max(pin.sum(), 1)
        # fraction of the frame height below the horizon -> rough N.V
        frac = (r0 + r1) / 2.0 / float(h)
        # grazing: N.V ~ frac * pitch-normalised; use the shader's own chain
        ndv = float(np.clip(frac * 0.55 + 0.03, 0.0, 1.0))
        f1 = 1.0 - ndv
        fr = min(0.022 + 0.978 * f1 ** 5, 0.92) * 1.25 + 0.045
        print(f'  r{y0 + r0:3d}-{y0 + r1:3d} | {band.mean():.3f} '
              f'| {100 * (g > 0.08).mean():5.2f} | {rim:5.1f} '
              f'| {100 * pale.mean():5.1f} | {min(fr, 0.92):.2f}')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)