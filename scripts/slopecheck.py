"""Analytic check: how steep is the scene-2 sea normal field, and how far does
sunDiffuse = dot(N, L) swing across a single wave?

The sun sits at (0.824, 0.287, 0.489) (scene2.cxx gSunDir), elevation 16.7 deg.
The wave field is the six sines in sea_frag.glsl's waveHeight(), and the normal
comes from their gradient. Everything sun-keyed in the shader -- pow(sunDiffuse,3)
on the warm slopes, the reflection azimuth, the GGX lobe -- is a function of that
one dot product, so its swing is the amplitude of the blotch contrast.

Wavelength/amplitude pairs are (freq cycles/m, amplitude m) from waveHeight().
"""
import numpy as np

SUN = np.array([0.824, 0.287, 0.489])
SUN = SUN / np.linalg.norm(SUN)
print("sun elevation %.2f deg  azimuth %.2f deg"
      % (np.degrees(np.arcsin(SUN[1])), np.degrees(np.arctan2(SUN[0], SUN[2]))))

OCT = [  # (dirx, dirz, wavenumber rad/m, amp m) -- k exactly as written in waveHeight()
    (0.98,  0.20, 0.170, 1.55),
    (-0.64, 0.77, 0.240, 1.00),
    (0.36, -0.93, 0.380, 0.55),
    (-0.91, -0.42, 0.540, 0.34),
    (0.59,  0.81, 0.860, 0.20),
    (-0.20, 0.98, 1.450, 0.11),
]

print("\nper-octave steepness A*k; a sane wind sea is 0.05..0.30:")
tot = 0.0
for dx, dz, k, a in OCT:
    sk = a * k
    tot += sk * sk
    print("  wavelength %6.1f m   amp %.2f m   A*k = %.3f  (%5.1f deg)"
          % (2 * np.pi / k, a, sk, np.degrees(np.arctan(sk))))
print("  rms slope (root sum of squares) = %.3f  -> %.1f deg"
      % (np.sqrt(tot), np.degrees(np.arctan(np.sqrt(tot)))))
print("  peak |grad| (coherent) ~ %.3f -> %.1f deg"
      % (sum(a * k for _, _, k, a in OCT),
         np.degrees(np.arctan(sum(a * k for _, _, k, a in OCT)))))


def grad_at(x, z, t):
    gx = gz = 0.0
    for dx, dz, k, a in OCT:
        ph = (x * dx + z * dz) * k
        gx += dx * (a * k) * np.cos(ph)
        gz += dz * (a * k) * np.cos(ph)
    return gx, gz


# Sample a patch of near-field water in front of the camera (cam y=7, pitch
# -0.05 rad, so the near field is roughly 8..60 m ahead).
n = 400
xs = np.linspace(0.0, 140.0, n)
zs = np.linspace(6.0, 120.0, n)
X, Z = np.meshgrid(xs, zs)
gx = np.zeros_like(X)
gz = np.zeros_like(X)
for dx, dz, k, a in OCT:
    ph = (X * dx + Z * dz) * k
    gx += dx * (a * k) * np.cos(ph)
    gz += dz * (a * k) * np.cos(ph)

slope = np.hypot(gx, gz)
nl = np.stack([-gx, np.ones_like(gx), -gz], axis=2)
nl /= np.linalg.norm(nl, axis=2, keepdims=True)
sd = np.clip(np.einsum("ijk,k->ij", nl, SUN), 0.0, None)

print("\nnear-field water 0..140 m ahead, all octave gates OPEN (foot -> 0):")
print("  |grad|  mean %.3f  p50 %.3f  p95 %.3f  max %.3f"
      % (slope.mean(), np.percentile(slope, 50),
         np.percentile(slope, 95), slope.max()))
print("  surface tilt from vertical: mean %.1f deg  p95 %.1f deg  max %.1f deg"
      % (np.degrees(np.arctan(slope)).mean(),
         np.degrees(np.arctan(np.percentile(slope, 95))),
         np.degrees(np.arctan(slope.max()))))
print("\n  sunDiffuse = max(dot(N,L),0):")
print("    mean %.3f  p05 %.3f  p50 %.3f  p95 %.3f  max %.3f"
      % (sd.mean(), np.percentile(sd, 5), np.percentile(sd, 50),
         np.percentile(sd, 95), sd.max()))
print("    fraction of the field at 0 (fully backlit slope): %.1f%%"
      % (100.0 * (sd < 1e-6).mean()))
print("    pow(sunDiffuse,3): mean %.3f  max %.3f"
      % ((sd ** 3).mean(), (sd ** 3).max()))
print("\n  ratio max(pow(sd,3)) / mean(pow(sd,3)) = %.1f  <- this is the"
      " blob contrast of the warm-slope term" % ((sd ** 3).max() / (sd ** 3).mean()))

# How many whole wave periods fit across the visible near field? If the dominant
# 37 m wave is the blob scale, that is the answer.
for dx, dz, k, a in OCT[:3]:
    print("  wavelength %.1f m -> %.1f periods across 140 m"
          % (2 * np.pi / k, 140.0 / (2 * np.pi / k)))