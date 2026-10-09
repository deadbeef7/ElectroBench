#!/usr/bin/env python3
"""Draw the ElectroBench LiveArea icon (128x128 PNG) with no image library.

The icon is the two ported scenes in one frame: a dusk horizon (scene 2) with
a power-line pole and its wire sag (scene 4). Written as a plain PNG with
zlib-compressed rows because the toolchain image has no PIL/ImageMagick.
"""
import struct
import sys
import zlib

W = H = 128


def lerp(a, b, t):
    return a + (b - a) * t


def pixel(x, y):
    """Colour of one pixel: sky gradient, sun band, water, pole, wire."""
    t = y / (H - 1)

    # Scene 2 sky: near-black indigo at the top into a maroon haze on the
    # horizon, then dark water below it.
    horizon = 0.52
    if t < horizon:
        k = t / horizon
        r = lerp(20, 150, k ** 2.4)
        g = lerp(12, 62, k ** 2.8)
        b = lerp(48, 70, k ** 2.0)
        # Sun glow just above the water line.
        glow = max(0.0, 1.0 - abs(t - horizon + 0.045) / 0.10)
        r += 90 * glow ** 2
        g += 52 * glow ** 2
        b += 10 * glow ** 2
    else:
        k = (t - horizon) / (1.0 - horizon)
        r = lerp(52, 6, k ** 0.6)
        g = lerp(26, 5, k ** 0.6)
        b = lerp(46, 20, k ** 0.6)

    # Scene 4 pole: a vertical mast on the right third with a cross-arm.
    px = 86
    if px - 3 <= x <= px + 3 and y > 18:
        shade = 0.72 + 0.28 * (1.0 - abs(x - px) / 3.0)
        r, g, b = 128 * shade, 96 * shade, 62 * shade
    if 66 <= y <= 70 and 74 <= x <= 118:
        r, g, b = 118, 88, 57

    # Wire: a catenary sag leaving the cross-arm to the left edge.
    sag = y - (66 + 26 * ((x - 74.0) / 44.0) ** 2)
    if 0 <= x < 74 and abs(sag) < 1.0:
        r, g, b = 196, 176, 140

    return (max(0, min(255, int(r))), max(0, min(255, int(g))), max(0, min(255, int(b))))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icon0.png"
    raw = bytearray()
    for y in range(H):
        raw.append(0)  # PNG filter type 0 (None) for this scanline
        for x in range(W):
            raw.extend(pixel(x, y))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))

    with open(out, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes)" % (out, len(png)))


if __name__ == "__main__":
    main()
