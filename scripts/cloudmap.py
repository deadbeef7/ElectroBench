#!/usr/bin/env python3
"""CPU replication of sea_frag.glsl's cloud-shadow field on the water plane.

Ports projectedCloudDensity() + cloudShadow() 1:1 (same hash, same ramps,
same constants from src/scene2.cxx) so the shadow footprints can be measured
in metres and the fix tuned without a 3-minute GPU render per iteration.

Usage: cloudmap.py T  [OUT.png]   -> top-down shadow map centred on the eye
"""
import sys
import numpy as np

# ---- constants straight from src/scene2.cxx -------------------------------
K_CLOUD_AZIM   = np.array([0.18, 0.88, 1.75, 2.65, 3.75, 4.65, 5.75, 0.30, 0.80])
K_CLOUD_DRIFT  = np.array([-0.0025, 0.0032, -0.0018, 0.0022, -0.0027, 0.0015, 0.0020, -0.0015, 0.0012])
K_CLOUD_ELEV   = np.array([0.190, 0.300, 0.250, 0.380, 0.220, 0.330, 0.160, 0.420, 0.465])
K_CLOUD_RAD    = np.array([0.048, 0.037, 0.030, 0.043, 0.027, 0.036, 0.050, 0.026, 0.030])
K_CLOUD_STRETCH= np.array([3.3, 2.4, 2.1, 2.8, 2.2, 2.3, 3.6, 4.6, 4.2])
MAX_CLOUDS     = 9
SUN_DIR        = np.array([0.824, 0.287, 0.489])
SUN_DIR       /= np.linalg.norm(SUN_DIR)
DECK_H         = 620.0   # cloud deck height used by cloudShadow()

# ---- GLSL helpers ----------------------------------------------------------
def fract(x):
    return x - np.floor(x)

def hash21(p):
    """vec2 -> float, identical arithmetic to the shader."""
    px, py = p[..., 0], p[..., 1]
    p3x = fract(px * 0.1031)
    p3y = fract(py * 0.1031)
    p3z = fract(px * 0.1031)          # p.xyx -> z == x
    dot = p3x * (p3y + 33.33) + p3y * (p3z + 33.33) + p3z * (p3x + 33.33)
    p3x += dot; p3y += dot; p3z += dot
    return fract((p3x + p3y) * p3z)

def cloud_value_noise(p):
    i = np.floor(p)
    f = p - i
    f = f * f * (3.0 - 2.0 * f)
    a = hash21(i)
    b = hash21(i + np.array([1.0, 0.0]))
    c = hash21(i + np.array([0.0, 1.0]))
    d = hash21(i + np.array([1.0, 1.0]))
    return (a * (1 - f[..., 0]) + b * f[..., 0]) * (1 - f[..., 1]) + \
           (c * (1 - f[..., 0]) + d * f[..., 0]) * f[..., 1]

def cloud_erosion(p):
    n = 0.57 * cloud_value_noise(p)
    p = np.stack([0.80 * p[..., 0] - 0.60 * p[..., 1],
                  0.60 * p[..., 0] + 0.80 * p[..., 1]], -1) * 2.03 + 17.1
    n += 0.29 * cloud_value_noise(p)
    p = np.stack([0.80 * p[..., 0] - 0.60 * p[..., 1],
                  0.60 * p[..., 0] + 0.80 * p[..., 1]], -1) * 2.01 + 11.7
    return n + 0.14 * cloud_value_noise(p)

def smoothmax(a, b, k):
    h = np.clip(0.5 + 0.5 * (a - b) / k, 0.0, 1.0)
    return b + (a - b) * h + k * h * (1.0 - h)

def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

# ---- the density field, ported line-for-line -------------------------------
OFFSETS = np.array([
    [ 0.00, 0.00, 0.00], [ 0.68, 0.02, 0.03], [-0.62, 0.10, -0.04],
    [ 0.25, 0.28, 0.06], [-0.27, 0.35, -0.02], [ 0.05, 0.53, 0.08],
    [ 0.43, -0.12, -0.07], [-0.40, -0.10, 0.05]])
RADII = np.array([0.55, 0.39, 0.37, 0.34, 0.31, 0.27, 0.30, 0.29])

def projected_cloud_density(dirv, t, edge0=0.035, edge1=0.78):
    """dirv: (..., 3) unit directions. Returns density in [0,1]."""
    density = np.zeros(dirv.shape[:-1])
    for i in range(MAX_CLOUDS):
        azim = K_CLOUD_AZIM[i] + K_CLOUD_DRIFT[i] * t
        c = np.array([np.cos(K_CLOUD_ELEV[i]) * np.cos(azim),
                      np.sin(K_CLOUD_ELEV[i]),
                      np.cos(K_CLOUD_ELEV[i]) * np.sin(azim)])
        t1 = np.array([-np.sin(c[2]), 0.0, np.cos(c[2])])
        t1 /= np.linalg.norm(t1)
        t2 = np.cross(c, t1); t2 /= np.linalg.norm(t2)
        delta = dirv - c
        p = np.stack([delta @ t1 / max(K_CLOUD_STRETCH[i], 1.0),
                      delta @ t2,
                      delta @ c], -1)
        R = max(K_CLOUD_RAD[i], 0.001)
        mask = (p * p).sum(-1) <= R * R * 3.1
        if not mask.any():
            continue
        pm = p[mask]
        local = np.zeros(pm.shape[0])
        for j in range(8):
            q = pm - OFFSETS[j] * R
            ang = np.arctan2(q[..., 1], q[..., 0])
            morph = (0.045 * np.sin(t * 0.10 + azim * 9.0 + j * 1.7)
                     + 0.035 * np.cos(t * 0.065 + azim * 5.0 + j * 2.9))
            rj = R * RADII[j] * (1.0
                + 0.11 * np.sin(ang * 3.0 + azim * 7.0 + j * 2.1)
                + 0.065 * np.sin(ang * 5.0 - azim * 11.0 + j * 4.7)
                + morph)
            lobe = cloud_erosion(np.stack([
                np.cos(ang) * 1.8 + azim * 3.7,
                np.sin(ang) * 1.8 + j * 2.1 + K_CLOUD_ELEV[i] * 9.0], -1))
            rj *= 0.91 + 0.16 * lobe
            metric = np.stack([q[..., 0] / rj, q[..., 1] / (rj * 0.92),
                               q[..., 2] / (rj * 1.18)], -1)
            local = smoothmax(local, 1.0 - sstep(0.62, 1.12, np.linalg.norm(metric, axis=-1)), 0.10)
        envP = pm[..., :2] / R
        ang = np.arctan2(envP[..., 1], envP[..., 0])
        envR = np.linalg.norm(envP, axis=-1) * (1.0 + 0.055 * np.sin(ang * 4.0 + azim * 13.0))
        envelope = 1.0 - sstep(0.96, 1.56, envR)
        n    = cloud_erosion(envP * 3.2 + np.array([azim * 5.1, i * 7.3]))
        fine = cloud_erosion(envP * 7.8 + np.array([azim * 11.0, i * 13.0]))
        shoulder = 1.0 - sstep(0.10, 0.82, local)
        local = sstep(edge0, edge1, local * (0.70 + 0.30 * n + 0.12 * fine - 0.20 * shoulder)) * envelope
        baseCut = sstep(-0.78, -0.48, envP[..., 1] + (n - 0.5) * 0.18)
        local = local * baseCut * (1.0 - 0.12 * sstep(0.48, 0.95, envP[..., 1]))
        density[mask] = smoothmax(density[mask], np.clip(local, 0.0, 1.0), 0.07)
    return np.clip(density, 0.0, 1.0)

def cloud_shadow(world_xz, eye, t, edge0=0.035, edge1=0.78, k=2.4, floor=0.12):
    """world_xz: (..., 2) sea positions. Returns the shadow factor [floor, 1]."""
    sd = SUN_DIR
    travel = np.maximum((DECK_H - 0.0) / max(sd[1], 0.15), 0.0)
    hit = np.zeros(world_xz.shape[:-1] + (3,))
    hit[..., 0] = world_xz[..., 0] - eye[0] + sd[0] * travel
    hit[..., 1] = 0.0        - eye[1] + sd[1] * travel
    hit[..., 2] = world_xz[..., 1] - eye[2] + sd[2] * travel
    hit /= np.linalg.norm(hit, axis=-1, keepdims=True)
    dens = projected_cloud_density(hit, t, edge0, edge1)
    return np.clip(np.exp(-dens * k), floor, 1.0)

def camera_eye(t):
    a = t * 0.16
    radius = 42.0 + np.sin(t * 0.042) * 10.0
    return np.array([np.cos(a) * radius,
                     7.5 + np.sin(t * 0.086) * 2.2,
                     np.sin(a) * radius * 0.7])

# ---- plate statistics on the shadow map ------------------------------------
def plate_stats(shadow, thresh=0.5):
    """Simple flood-fill plate count/size on the dark footprint mask."""
    from collections import deque
    dark = shadow < thresh
    h, w = dark.shape
    seen = np.zeros_like(dark, bool)
    sizes = []
    for y in range(0, h, 2):          # stride 2: cap the Python cost
        for x in range(w):
            if dark[y, x] and not seen[y, x]:
                q = deque([(y, x)]); seen[y, x] = True; n = 0
                while q:
                    cy, cx = q.popleft(); n += 1
                    for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                        ny, nx = cy+dy, cx+dx
                        if 0 <= ny < h and 0 <= nx < w and dark[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True; q.append((ny, nx))
                sizes.append(n * 4)   # stride-2 correction
    sizes = np.array(sizes)
    return len(sizes), (np.median(sizes) if len(sizes) else 0), (sizes.max() if len(sizes) else 0)

def main():
    t = float(sys.argv[1]) if len(sys.argv) > 1 else 36.0
    out = sys.argv[2] if len(sys.argv) > 2 else None
    eye = camera_eye(t)
    N = 700
    half = 260.0   # metres each side
    xs = np.linspace(eye[0] - half, eye[0] + half, N)
    zs = np.linspace(eye[2] - half, eye[2] + half, N)
    X, Z = np.meshgrid(xs, zs)
    sh = cloud_shadow(np.stack([X, Z], -1), eye, t)
    # print stats over the region the camera actually looks at (sun azimuth ±60 deg)
    fwd = np.array([SUN_DIR[0], 0.0, SUN_DIR[2]]); fwd /= np.linalg.norm(fwd)
    right = np.array([fwd[2], 0.0, -fwd[0]])
    rel = np.stack([X - eye[0], Z - eye[2]], -1)
    fwd2 = np.array([fwd[0], fwd[2]])
    right2 = np.array([right[0], right[2]])
    along = rel @ fwd2; side = rel @ right2
    view = (along > 8) & (along < 260) & (np.abs(side) < 0.7 * along)   # ~±35 deg
    sv = sh[view]
    print(f"t={t}  eye=({eye[0]:.1f},{eye[1]:.1f},{eye[2]:.1f})")
    print(f"  shadow in view: mean={sv.mean():.3f}  min={sv.min():.3f}")
    print(f"  dark fraction (<0.5): {(sv < 0.5).mean()*100:.1f}%   (<0.25): {(sv < 0.25).mean()*100:.1f}%")
    # 1-D contrast across a horizontal transect at 40 m out
    row = np.argmin(np.abs(along[:, N//2] - 40.0))
    tran = sh[row]
    d1 = np.abs(np.diff(tran))
    print(f"  transect @40m: {int((d1 > 0.3).sum())} jumps >0.3, max step {d1.max():.2f}")
    nc, med, mx = plate_stats(sh)
    print(f"  plates (whole map): count={nc} median={med}px max={mx}px (2 m/px)")
    if out:
        try:
            from PIL import Image
            g = (np.clip(sh, 0, 1) * 255).astype(np.uint8)
            Image.fromarray(g).save(out)
            print(f"  wrote {out}")
        except ImportError:
            print("  (no PIL - skipped png)")

if __name__ == "__main__":
    main()
