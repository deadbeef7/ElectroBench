#!/usr/bin/env python3
"""Draw the ElectroBench launcher icons -- one flat mark, three platforms.

The mark is drawn from primitives, not sampled from a photograph: the scene-4
utility pole, its three crossarms and two sagging wires in ink, an ember sun
setting behind it, and a hard indigo band below the horizon. The pole and its
right-hand crossarms also spell the E of ElectroBench, which is what keeps the
silhouette readable when the icon is 48x48 (Android mdpi) or the 144x80 PSP
ICON0. Four colours, no gradients, no glow, no soft shadows.

Everything is written here rather than in the three port scripts because the
ports must ship the *same* icon; a per-port copy is how they drift. The two
colour depths exist for platform reasons:

  * PSP ICON0 and the Vita LiveArea icon must be 8-bit palettised PNGs.
    The Vita package promoter rejects truecolour icon0.png with 0x8010113D, so
    that is a hard requirement, not an aesthetic one.
  * Android takes RGBA and gets a proper mipmap per density instead of one
    256x256 bitmap scaled by the launcher.

Anti-aliasing is a 4x4 box filter over an analytic shape test; no image
library is imported, because the build host for the PSP/Vita images may not
have one.

Usage:
  python3 buildtools/make_icons.py vita  OUT.png          # 128x128, indexed
  python3 buildtools/make_icons.py psp   OUT.png          # 144x80,  indexed
  python3 buildtools/make_icons.py android OUT.png --size 96
"""
import struct
import sys
import zlib

# ---------------------------------------------------------------- the palette
CREAM = (237, 227, 206)
INK = (17, 23, 34)
INDIGO = (27, 43, 68)
EMBER = (208, 94, 48)

SS = 4  # supersampling grid per axis


def lerp(a, b, t):
    return a + (b - a) * t


def catenary_y(xa, ya, xb, yb, sag, x):
    """Quadratic sag between two wire support points, max drop in the middle."""
    t = (x - xa) / (xb - xa)
    return lerp(ya, yb, t) + sag * 4.0 * t * (1.0 - t)


def square_icon(u, v):
    """Colour at normalised (u, v) in [0,1]^2 for the square badge."""
    horizon = 0.780
    # Sun, upper half only: its centre sits exactly on the horizon, and the
    # pole is drawn over it so the silhouette does the work at small sizes.
    sx, sy, sr = 0.335, horizon, 0.170
    if v < horizon and (u - sx) ** 2 + (v - sy) ** 2 <= sr * sr:
        return EMBER

    # Shaft, then the three arms that make the E. Arms are drawn before the
    # wires so a wire leaves from the arm tip cleanly.
    if 0.214 <= u <= 0.252 and 0.092 <= v <= horizon:
        return INK
    if 0.236 <= u <= 0.478 and 0.154 <= v <= 0.196:
        return INK
    if 0.236 <= u <= 0.512 and 0.326 <= v <= 0.368:
        return INK
    if 0.236 <= u <= 0.456 and 0.498 <= v <= 0.540:
        return INK

    # Two conductors leaving the upper arms. The half-width is deliberately
    # 1.2 px at 128, so the 48px build still resolves them.
    if 0.478 < u <= 1.0:
        wy = catenary_y(0.478, 0.196, 1.0, 0.300, 0.050, u)
        if abs(v - wy) <= 0.016:
            return INK
    if 0.512 < u <= 1.0:
        wy = catenary_y(0.512, 0.368, 1.0, 0.430, 0.038, u)
        if abs(v - wy) <= 0.016:
            return INK

    return INDIGO if v >= horizon else CREAM


def wide_icon(u, v):
    """The same mark in the PSP's 144x80 aspect (a wider field, same horizon)."""
    horizon = 0.620
    sx, sy, sr = 0.300, horizon, 0.205
    # The sun is a circle in pixels, not in normalised units: on a 144x80 frame
    # the u and v scales differ by 1.8, so the radius is taken in heights and
    # u is stretched by the aspect ratio before the distance test.
    if v < horizon:
        dx = (u - sx) * (144.0 / 80.0)
        if dx * dx + (v - sy) ** 2 <= sr * sr:
            return EMBER

    if 0.176 <= u <= 0.203 and 0.060 <= v <= horizon:
        return INK
    if 0.192 <= u <= 0.392 and 0.148 <= v <= 0.182:
        return INK
    if 0.192 <= u <= 0.422 and 0.330 <= v <= 0.364:
        return INK
    if 0.192 <= u <= 0.372 and 0.512 <= v <= 0.546:
        return INK

    if 0.392 < u <= 1.0:
        wy = catenary_y(0.392, 0.182, 1.0, 0.300, 0.055, u)
        if abs(v - wy) <= 0.018:
            return INK
    if 0.422 < u <= 1.0:
        wy = catenary_y(0.422, 0.364, 1.0, 0.440, 0.040, u)
        if abs(v - wy) <= 0.018:
            return INK

    return INDIGO if v >= horizon else CREAM


def render(w, h, wide):
    """Returns an RGBA byte buffer, top row first."""
    out = bytearray(w * h * 4)
    inv = 1.0 / (SS * SS)
    for y in range(h):
        for x in range(w):
            r = g = b = 0.0
            for sy in range(SS):
                v = (y + (sy + 0.5) / SS) / h
                for sx in range(SS):
                    u = (x + (sx + 0.5) / SS) / w
                    cr, cg, cb = wide_icon(u, v) if wide else square_icon(u, v)
                    r += cr
                    g += cg
                    b += cb
            o = (y * w + x) * 4
            out[o + 0] = int(r * inv + 0.5)
            out[o + 1] = int(g * inv + 0.5)
            out[o + 2] = int(b * inv + 0.5)
            out[o + 3] = 255
    return out


def png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_indexed(path, w, h, rgba):
    """8-bit palette PNG (colour type 3) -- required by PSP and the Vita."""
    palette = {}
    indices = bytearray(w * h)
    order = []
    for i in range(w * h):
        r, g, b = rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]
        key = (r >> 3, g >> 3, b >> 2)
        idx = palette.get(key)
        if idx is None:
            # Index 0 is reserved: a palette PNG must always have at least one
            # entry, and a transparent-looking black is the least surprising
            # filler for the reserved slot.
            if len(palette) >= 255:
                idx = 0
            else:
                idx = len(palette) + 1
                palette[key] = idx
                order.append(key)
        indices[i] = idx

    plte = bytearray(3 * 256)
    for key, idx in palette.items():
        r5, g5, b6 = key
        plte[3 * idx + 0] = (r5 << 3) | (r5 >> 2)
        plte[3 * idx + 1] = (g5 << 3) | (g5 >> 2)
        plte[3 * idx + 2] = (b6 << 2) | (b6 >> 4)

    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type None
        raw.extend(indices[y * w:(y + 1) * w])

    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)) +
           png_chunk(b"PLTE", bytes(plte)) +
           png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    return len(png), len(palette)


def write_rgba(path, w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * w * 4:(y + 1) * w * 4])
    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    kind = args[0]
    out = args[1] if len(args) > 1 else "icon.png"
    size = None
    for i, a in enumerate(args):
        if a == "--size" and i + 1 < len(args):
            size = int(args[i + 1])

    if kind == "vita":
        w = h = 128
        rgba = render(w, h, wide=False)
        n, colours = write_indexed(out, w, h, rgba)
        print("wrote %s (%dx%d, %d bytes, %d-colour palette)" % (out, w, h, n, colours))
    elif kind == "psp":
        w, h = 144, 80
        rgba = render(w, h, wide=True)
        n, colours = write_indexed(out, w, h, rgba)
        print("wrote %s (%dx%d, %d bytes, %d-colour palette)" % (out, w, h, n, colours))
    elif kind == "android":
        w = h = size or 256
        rgba = render(w, h, wide=False)
        n = write_rgba(out, w, h, rgba)
        print("wrote %s (%dx%d, %d bytes, RGBA)" % (out, w, h, n))
    else:
        print("unknown icon kind: %s (want vita|psp|android)" % kind)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
