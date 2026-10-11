#!/usr/bin/env python3
"""Draw the ElectroBench LiveArea icon (128x128 PNG) with no image library.

The icon is the two ported scenes in one frame: a dusk horizon (scene 2) with
a power-line pole and its wire sag (scene 4). Written as a plain PNG with
zlib-compressed rows because the toolchain image has no PIL/ImageMagick.

Output is an 8-bit palettized PNG (color type 3, no alpha): the Vita package
promoter rejects truecolor PNGs during VPK install with error 0x8010113D, so
LiveArea images must be 256-color indexed.
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

    # Sample every pixel, keep a 5-6-5-style quantization so the palettized
    # icon stays visually faithful to the gradient.
    palette = {}
    indices = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = pixel(x, y)
            key = (r >> 3, g >> 3, b >> 2)
            idx = palette.get(key)
            if idx is None:
                # Reserve entry 0 as transparent-equivalent black; palette index
                # 0 must always exist in a palettized PNG.
                idx = len(palette) + 1 if len(palette) < 255 else 0
                palette[key] = idx
            indices.append(idx)

    # Build the actual PLTE list from the palette dict.
    plte = bytearray(3 * 256)
    for key, idx in palette.items():
        r5, g5, b6 = key
        plte[3 * idx + 0] = (r5 << 3) | (r5 >> 2)
        plte[3 * idx + 1] = (g5 << 3) | (g5 >> 2)
        plte[3 * idx + 2] = (b6 << 2) | (b6 >> 4)
    plte[0:3] = b"\x00\x00\x00"

    raw = bytearray()
    pos = 0
    for y in range(H):
        raw.append(0)  # PNG filter type 0 (None) for this scanline
        raw.extend(indices[pos:pos + W])
        pos += W

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 3, 0, 0, 0)) +
           chunk(b"PLTE", bytes(plte)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))

    with open(out, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes, bitdepth=8 colortype=3 palette=%d)" % (out, len(png), len(palette)))


if __name__ == "__main__":
    main()
