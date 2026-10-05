"""Localise the P24 sea change: step vs scale, per distance row.

Averaging the step over the whole sea can wash the change out, because the
shader only touches water whose slope spread is large -- i.e. the FAR water,
where one pixel covers most of a wave. This walks down the water band in
equal row groups and reports the 4/16/64 px step per group, before vs after.

Usage: p24rows.py <dir-with-before-*.ppm-and-after-*.ppm>
"""
import sys
import numpy as np

LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
PHASES = (32, 36, 40)


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


def rowgroups(a, ngroups=6):
    """-> list of (label, step4, step16, step64) for ngroups bands of water."""
    lum = a @ LUM
    hy = horizon(lum)
    h, w = lum.shape
    out = []
    gh = max(64, (h - hy) // ngroups)
    for g in range(ngroups):
        y0 = hy + g * gh
        y1 = min(h, y0 + gh)
        if y1 - y0 < 64:
            break
        sub = lum[y0:y1, int(w * 0.25):int(w * 0.75)]
        row = []
        for k in (4, 8, 16):
            hh = sub.shape[0] // k * k
            ww = sub.shape[1] // k * k
            if hh < 2 * k or ww < 2 * k:
                row.append(float("nan"))
                continue
            blk = sub[:hh, :ww].reshape(hh // k, ww // k, k, k).mean(axis=(2, 3))
            row.append(float((np.abs(np.diff(blk, axis=1)).mean() +
                              np.abs(np.diff(blk, axis=0)).mean()) * 0.5))
        out.append(("rows %d-%d" % (y0, y1), row, float(sub.mean())))
    return out


def main():
    d = sys.argv[1]
    agg = {}
    for tag in ("before", "after"):
        acc = []
        for t in PHASES:
            acc.append(rowgroups(load("%s/%s-%d.ppm" % (d, tag, t))))
        n = min(len(a) for a in acc)
        agg[tag] = [(acc[0][g][0],
                     [float(np.nanmean([a[g][1][i] for a in acc])) for i in range(3)],
                     float(np.nanmean([a[g][2] for a in acc])))
                    for g in range(n)]

    print("mean over t=%s   step is %% of that band's own mean luma"
          % ",".join(map(str, PHASES)))
    print("%-14s %8s | %-20s | %-20s" % ("band", "meanlum", "BEFORE  4/8/16 px",
                                        "AFTER   4/8/16 px"))
    for g in range(len(agg["before"])):
        lb, vb, mb = agg["before"][g]
        la, va, ma = agg["after"][g]
        fb = "  %5.2f %5.2f %5.2f" % tuple(100 * x / max(mb, 1e-6) for x in vb)
        fa = "  %5.2f %5.2f %5.2f" % tuple(100 * x / max(ma, 1e-6) for x in va)
        dl = [100 * ((y / max(ma, 1e-6)) / (x / max(mb, 1e-6)) - 1)
              for x, y in zip(vb, va)]
        print("%-14s %8.4f |%s |%s   delta %+6.1f%% %+6.1f%% %+6.1f%%"
              % (lb.split()[1], mb, fb, fa, dl[0], dl[1], dl[2]))

    # 32/64 px need >= 128 rows, so they are measured on one tall far-water
    # region instead of per band: the top 192 rows of the sea, which is exactly
    # where the sub-pixel slope spread is large enough to move the change.
    tall = {}
    for tag in ("before", "after"):
        acc = []
        for t in PHASES:
            a = load("%s/%s-%d.ppm" % (d, tag, t))
            lum = a @ LUM
            hy = horizon(lum)
            h, w = lum.shape
            sub = lum[hy:hy + 192, int(w * 0.25):int(w * 0.75)]
            row = []
            for k in (32, 64):
                hh = sub.shape[0] // k * k
                ww = sub.shape[1] // k * k
                blk = sub[:hh, :ww].reshape(hh // k, ww // k, k, k).mean(axis=(2, 3))
                row.append(float((np.abs(np.diff(blk, axis=1)).mean() +
                                  np.abs(np.diff(blk, axis=0)).mean()) * 0.5))
            acc.append((row, float(sub.mean())))
        tall[tag] = ([float(np.mean([x[0][i] for x in acc])) for i in range(2)],
                     float(np.mean([x[1] for x in acc])))
    print("\nfar water rows 0-192 under the horizon, tall enough for the big scales")
    vb, mb = tall["before"]
    va, ma = tall["after"]
    for i, k in enumerate((32, 64)):
        print("  %2d px step   before %5.2f%%   after %5.2f%%   delta %+6.1f%%"
              % (k, 100 * vb[i] / mb, 100 * va[i] / ma,
                 100 * ((va[i] / ma) / (vb[i] / mb) - 1)))


main()