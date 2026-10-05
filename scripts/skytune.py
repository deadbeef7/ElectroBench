#!/usr/bin/env python3
"""Solve the sky constants for a DISPLAYED orange, not a linear one.

The dome is authored in linear HDR and displayed through ACES + gamma 1/2.2.
That chain is what breaks "orange": a linear colour at hue 11 deg (deeply
orange, ratio 1.00 : 0.20 : 0.024) tone-maps to hue 36 deg, because gamma lifts
the near-black green and blue channels far more than the bright red. So a
linear fix that looks right on paper reads amber on screen. This solves on the
DISPLAYED value, which is the only thing the eye ever sees.

  skytune.py            sweep the current model
"""
import math
import numpy as np

K_BETA_R = np.array([0.058, 0.135, 0.331])
SUN = np.array([-0.30, 0.20, 0.93])
SUN = SUN / np.linalg.norm(SUN)
# elevations the 52-degree camera actually frames, and azimuths across the view
ELEVS = [3, 8, 14, 20, 26]
AZIMS = [0, 60, 120, 180]


def hue(rgb):
    r, g, b = [float(v) for v in rgb]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d <= 1e-9:
        return 0.0
    if mx == r:
        return 60.0 * (((g - b) / d) % 6.0)
    if mx == g:
        return 60.0 * (((b - r) / d) + 2.0)
    return 60.0 * (((r - g) / d) + 4.0)


def encode_sky(hdr):
    """ACES Narkowicz + gamma 1/2.2, matching sky_frag.glsl."""
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    x = np.clip(hdr, 0.0, 8.0)
    x = np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)
    return np.power(x, 1.0 / 2.2)


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def dome(h, azim, k_sun_path, k_ray_gain, k_sun_i, k_beta_m,
         air_r, air_g, air_b, air_r2, air_g2, air_b2, air_fall):
    e = math.sin(math.radians(h))
    r = math.cos(math.radians(h))
    a = math.radians(azim)
    dirv = np.array([r * math.cos(a), e, r * math.sin(a)])
    mu = float(np.dot(dirv, SUN))
    hs = max(h if h > 0 else 0.0, 0.0)
    yy = max(dirv[1], 0.0)
    m_view = 1.0 / (yy + 0.14)
    m_sun = 1.0 / (max(SUN[1], 0.0) + 0.14)
    t_view = np.exp(-(K_BETA_R + k_beta_m) * m_view * 0.92)
    t_sun = np.exp(-K_BETA_R * m_sun * k_sun_path - k_beta_m * m_sun * 1.15)
    ph_r = 0.0596831 * (1.0 + mu * mu)
    g = 0.76
    gg = g * g
    ph_m = 0.0795775 * (1.0 - gg) / max((1.0 + gg - 2.0 * g * mu) ** 1.5, 1e-3)
    single = K_BETA_R * ph_r * k_ray_gain * t_view * t_sun
    mie = np.array([k_beta_m * ph_m * 0.25]) * t_view * t_sun
    multi_k = 0.005 + 0.30 * smoothstep(0.78, 1.06, yy)
    multi = K_BETA_R * ph_r * 0.80 * t_view * multi_k
    air = (np.array([air_r, air_g, air_b])
           + np.array([air_r2, air_g2, air_b2]) * max(mu, 0.0) ** 3)
    air = air * math.exp(-max(yy, 0.0) * air_fall)
    return encode_sky((single + mie + multi) * k_sun_i + air)


CURRENT = dict(k_sun_path=9.8, k_ray_gain=4.35, k_sun_i=330.0, k_beta_m=0.0092,
               air_r=0.450, air_g=0.121, air_b=0.031,
               air_r2=0.480, air_g2=0.147, air_b2=0.038, air_fall=5.6)


def stats(p):
    hs, vs = [], []
    for h in ELEVS:
        for a in AZIMS:
            c = dome(h, a, **p)
            hs.append(hue(c))
            vs.append(float(np.mean(c)))
    return float(np.mean(hs)), max(hs), float(np.mean(vs))


def report(name, p):
    m, mx, v = stats(p)
    print(f"{name:34s} mean hue {m:5.1f}  worst {mx:5.1f}  mean val {v:.3f}")
    return m, v


if __name__ == "__main__":
    report("CURRENT (P15)", CURRENT)
    print("\n-- sweep kSunPath, kRayGain scaled to hold brightness --")
    base_v = stats(CURRENT)[2]
    for sp in (9.8, 14.0, 18.0, 22.0, 26.0):
        for gain_scale in (1.0, 1.6, 2.2, 3.0):
            p = dict(CURRENT, k_sun_path=sp, k_ray_gain=CURRENT["k_ray_gain"] * gain_scale)
            m, _mx, v = stats(p)
            flag = "  <-- val held" if abs(v - base_v) < 0.03 else ""
            print(f"  kSunPath {sp:5.1f} gain x{gain_scale:4.1f} ({p['k_ray_gain']:5.1f}): "
                  f"mean hue {m:5.1f}  val {v:.3f}{flag}")
