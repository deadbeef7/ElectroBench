"""Decomposition tag: which additive piece of the sea colour makes the blotch?

Panels across a 760 px frame:
  0 body*(1-fresnel)   1 reflColor*fresnel   2 glitter add
  3 foam add           4 the real shaded frame (reference)

Everything is reported on the WATER only (the tag pass paints the sky black, so
the horizon is found from the frame). For each panel it reports the block-level
contrast in the sea and how well that panel predicts the reference's own
lit/dark split. The panel with the highest agreement is the term to fix.
"""
import sys

import numpy as np

NAMES = ["body*(1-F)", "refl*F", "glitter", "foam", "REFERENCE"]


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def main():
    a = load(sys.argv[1])
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    pw = w // 5
    P = [lum[:, i * pw:(i + 1) * pw] for i in range(5)]

    # horizon: the tag pass zeroes the sky, so the topmost sea row is where the
    # panels first light up. Ignore the HUD, which lives in the top rows.
    col = P[4].mean(axis=1)
    on = np.nonzero(col > 0.01)[0]
    hy = int(on[0]) if on.size else 0
    # walk down to the first row that stays lit (the horizon band is thin)
    for y in range(hy, h):
        if P[4][y:min(y + 3, h)].mean() > 0.01:
            hy = y
            break
    print("frame %dx%d  sea starts at row %d (%.1f%% down)"
          % (w, h, hy, 100.0 * hy / h))

    bs = 4
    ny = (h - hy) // bs
    nx = P[0].shape[1] // bs
    # de-gamma the sqrt() the tag pass applied
    B = [(P[i][hy:hy + ny * bs, :nx * bs]
          .reshape(ny, bs, nx, bs).mean(axis=(1, 3)) ** 2) for i in range(5)]

    print("\nterm                mean      std    blockstep   share of sea luma")
    tot = B[4].mean()
    for i, nm in enumerate(NAMES):
        st = np.concatenate([np.abs(np.diff(B[i], axis=1)).ravel(),
                             np.abs(np.diff(B[i], axis=0)).ravel()])
        print("  %-16s %.5f  %.5f   %.5f     %5.1f%%"
              % (nm, B[i].mean(), B[i].std(), st.mean(),
                 100.0 * B[i].mean() / tot))

    # Local contrast of the reference: this IS the blotch amplitude.
    ref = B[4]
    step = np.concatenate([np.abs(np.diff(ref, axis=1)).ravel(),
                           np.abs(np.diff(ref, axis=0)).ravel()])
    print("\nREFERENCE block-to-block step: mean %.5f  p95 %.5f  p99 %.5f"
          % (step.mean(), np.percentile(step, 95), np.percentile(step, 99)))
    print("  (for scale, the sun column mean is %.5f, so the blotch step is"
          " %.0f%% of it)"
          % (ref.mean(), 100.0 * step.mean() / max(ref.mean(), 1e-6)))

    print("\nagreement with the reference's lit/dark structure")
    print("  (% of reference block variance explained, after the best fit)")
    refc = ref - ref.mean()
    rv = float(refc @ refc)
    for i, nm in enumerate(NAMES):
        b = B[i]
        bc = b - b.mean()
        bb = float(bc @ bc)
        if bb <= 0:
            print("  %-16s  no structure" % nm)
            continue
        k = float(bc @ refc) / bb
        print("  %-16s  corr %+.3f   explains %5.1f%% of reference variance"
              % (nm, float(bc @ refc) / (np.sqrt(bb * rv) + 1e-9),
                 100.0 * k * k * bb / rv))

    # Where in the frame is the blotch worst? Row band -> reference step.
    print("\nreference block step by depth band (worst band is the artifact):")
    rows = ny // 6
    for j in range(6):
        y0, y1 = j * rows, (j + 1) * rows
        if y1 <= y0:
            continue
        s = ref[y0:y1]
        st = np.concatenate([np.abs(np.diff(s, axis=1)).ravel(),
                             np.abs(np.diff(s, axis=0)).ravel()])
        print("   frame rows %3d..%3d   step %.5f  mean luma %.5f"
              % (hy + y0 * bs, hy + y1 * bs - 1, st.mean(), s.mean()))


main()