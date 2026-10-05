"""Second tag pass: does the FOAM texture carry the blotch shape?

Panels across a 760 px frame:
  0 raw foam    1 foam as it is added to body    2 chaos tile
  3 crest*slopeFacing gate    4 the real shaded frame (reference)

The question is only about panel 4: its lit/dark patch pattern is the artifact.
So each term is correlated against panel 4's own local structure -- if a term
reproduces panel 4's patches, it is the cause.
"""
import sys

import numpy as np

NAMES = ["foam", "foamLit*foam", "chaos", "crest*slopeFacing", "REFERENCE"]


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

    bs = 4          # 4 px blocks, one per foam-tile feature
    ny, nx = P[0].shape[0] // bs, P[0].shape[1] // bs
    B = [p[:ny * bs, :nx * bs].reshape(ny, bs, nx, bs).mean(axis=(1, 3))
         for p in P]

    print("per panel over the whole sea:")
    for i, nm in enumerate(NAMES):
        p = P[i]
        st = np.concatenate([np.abs(np.diff(B[i], axis=1)).ravel(),
                             np.abs(np.diff(B[i], axis=0)).ravel()])
        print("  %d %-19s mean %.4f std %.4f | block step mean %.5f "
              "p99 %.5f" % (i, nm, p.mean(), p.std(), st.mean(),
                            np.percentile(st, 99)))

    # Blotch metric: separate the reference frame's sea into "lit" and "dark"
    # by its own median, then ask which term best predicts lit vs dark.
    # All of this is done on the BLOCK maps so the masks line up.
    ref = B[4]
    lit = ref > np.median(ref)
    print("\nreference frame: sea luma median %.4f, 'lit' share %.1f%%"
          % (np.median(ref), 100.0 * lit.mean()))

    print("\nseparability of lit/dark by each term")
    print("  (|mean(lit) - mean(dark)| / pooled std; higher = better predictor)")
    for i, nm in enumerate(NAMES):
        b = B[i]
        ml, md = b[lit], b[~lit]
        pooled = np.sqrt(0.5 * (ml.var() + md.var())) + 1e-6
        d = abs(ml.mean() - md.mean()) / pooled
        a1 = (b - b.mean()).ravel()
        b1 = (ref - ref.mean()).ravel()
        corr = float(a1 @ b1 / (np.linalg.norm(a1) * np.linalg.norm(b1) + 1e-9))
        print("  %d %-19s cohen_d %.3f   corr with reference %+.3f"
              % (i, nm, d, corr))

    # Does the foam texture's amplitude stay high far out? If so the gate
    # octaveRes(foot, 0.62) is not touching the coarse octaves that matter.
    # Panels are gamma-encoded with sqrt(), so square to recover the linear
    # value the shader actually computed.
    b = B[0] ** 2
    rows = ny // 8
    print("\nfoam amplitude by depth band, de-gamma'd (row band -> mean, std, step):")
    for k in range(8):
        y0, y1 = k * rows, (k + 1) * rows
        if y1 <= y0:
            continue
        s = b[y0:y1]
        st = np.concatenate([np.abs(np.diff(s, axis=1)).ravel(),
                             np.abs(np.diff(s, axis=0)).ravel()])
        print("   rows %3d..%3d  mean %.5f  std %.5f  step %.5f"
              % (y0 * bs, y1 * bs, s.mean(), s.std(), st.mean()))
    print("\nfoam linear mean %.5f  p50 %.5f  p95 %.5f  max %.5f"
          % (b.mean(), np.percentile(b, 50), np.percentile(b, 95), b.max()))
    print("chaos tile, de-gamma'd: mean %.5f p05 %.5f p95 %.5f"
          % ((B[2] ** 2).mean(), np.percentile(B[2] ** 2, 5),
             np.percentile(B[2] ** 2, 95)))


main()