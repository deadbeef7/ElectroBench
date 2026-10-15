#!/usr/bin/env python3
"""Draw the ElectroBench launcher icon (256x256 RGBA PNG), no image library.

Same frame as the Vita LiveArea icon in tools/make_icon.py -- a dusk horizon
(scene 2) with a power-line pole and its sagging wire (scene 4) -- but written
as a truecolour RGBA PNG with a plain zlib stream, because Android's package
manager has no opinion about indexed colour and the toolchain image has no
PIL/ImageMagick.

Usage: python3 android/tools/make_android_icon.py OUT.png
"""
import struct
import sys
import zlib

W = H = 256
HORIZON = 150.0


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c0, c1, t):
    return tuple(lerp(c0[i], c1[i], t) for i in range(3))


def wire_y(x):
    """Scene 4's catenary, hanging from the crossbar to the right edge."""
    t = max(0.0, min(1.0, (x - 60.0) / 196.0))
    return 82.0 + 26.0 * (t * t)


def pixel(x, y):
    fx, fy = float(x), float(y)

    # ---- sky: near-black indigo at the top into a maroon haze at the horizon
    if fy < HORIZON:
        t = fy / HORIZON
        col = mix((10, 12, 34), (168, 74, 62), t ** 0.8)
        # sun: a soft disc just above the horizon, scene 2's low dusk sun
        dx, dy = fx - 176.0, fy - 118.0
        r = (dx * dx + dy * dy) ** 0.5
        if r < 46.0:
            glow = max(0.0, 1.0 - r / 46.0) ** 2
            col = mix(col, (255, 208, 138), min(1.0, glow * 1.15))
        if r < 16.0:
            col = mix(col, (255, 240, 200), 0.85)
    else:
        # ---- water: dark, with the sun's reflection broken into bands
        t = (fy - HORIZON) / (H - HORIZON)
        col = mix((34, 30, 40), (12, 14, 26), t ** 0.6)
        band = abs(((fy - HORIZON) % 14.0) - 7.0) / 7.0
        streak = max(0.0, 1.0 - abs(fx - 176.0) / (52.0 + 90.0 * t))
        glow = (1.0 - band) * streak * (1.0 - t)
        col = mix(col, (255, 170, 96), min(1.0, glow * 0.9))

    # ---- scene 4's pole: mast, crossbar, two wires
    if 60.0 <= fx <= 66.0 and 60.0 <= fy <= 214.0:
        col = (26, 24, 30)
    if 46.0 <= fx <= 104.0 and 78.0 <= fy <= 84.0:
        col = (26, 24, 30)
    for offset in (0.0, 9.0):
        if fy >= wire_y(fx) + offset - 1.6 and fy <= wire_y(fx) + offset + 1.6 and fx >= 62.0:
            col = (16, 15, 20)

    return col


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "ic_launcher.png"

    rows = bytearray()
    for y in range(H):
        rows.append(0)  # PNG filter type 0 for every row
        for x in range(W):
            r, g, b = pixel(x, y)
            rows += bytes((int(r + 0.5) & 0xFF, int(g + 0.5) & 0xFF, int(b + 0.5) & 0xFF, 255))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")

    with open(out_path, "wb") as f:
        f.write(png)
    print("wrote %s (%dx%d, %d bytes)" % (out_path, W, H, len(png)))


if __name__ == "__main__":
    main()
