#!/usr/bin/env python3
"""Draw the ElectroBench PSP ICON0 (144x80 PNG, 256-colour palette).

Same drawing as tools/make_icon.py (the dusk horizon, sun band and water) but
in the PSP's icon aspect. Plain PNG via zlib because the toolchain image has
no PIL/ImageMagick.
"""
import struct
import sys
import zlib

W, H = 144, 80


def lerp(a, b, t):
    return a + (b - a) * t


def pixel(x, y):
    t = y / (H - 1)
    horizon = 0.50
    if t < horizon:
        k = t / horizon
        r = lerp(16, 150, k ** 2.4)
        g = lerp(10, 62, k ** 2.8)
        b = lerp(40, 70, k ** 2.0)
        glow = max(0.0, 1.0 - abs(t - horizon + 0.05) / 0.12)
        r += 92 * glow ** 2
        g += 54 * glow ** 2
        b += 10 * glow ** 2
        # Sun disc just above the water line, matching scene 2's composition.
        dx = (x - W * 0.62) / (W * 0.055)
        dy = (y - H * 0.40) / (H * 0.10)
        d = (dx * dx + dy * dy) ** 0.5
        if d < 1.0:
            k2 = 1.0 - d
            r += 120 * k2
            g += 78 * k2
            b += 26 * k2
        # One warm band on the horizon.
        band = max(0.0, 1.0 - abs(t - horizon + 0.085) / 0.018)
        r += 70 * band
        g += 30 * band
    else:
        k = (t - horizon) / (1.0 - horizon)
        r = lerp(58, 6, k ** 0.6)
        g = lerp(28, 5, k ** 0.6)
        b = lerp(50, 22, k ** 0.6)
        # Glitter path under the sun.
        dx = abs(x - W * 0.62) / (W * 0.20)
        if k > 0.05 and dx < 1.0:
            g = min(255, g + (34 + 40 * (1.0 - k)) * (1.0 - dx))
            r = min(255, r + (52 + 60 * (1.0 - k)) * (1.0 - dx))
            b = min(255, b + 12 * (1.0 - dx))
    return (max(0, min(255, int(r))), max(0, min(255, int(g))), max(0, min(255, int(b))))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "ICON0.PNG"
    palette = {}
    indices = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = pixel(x, y)
            key = (r >> 3, g >> 3, b >> 2)
            idx = palette.get(key)
            if idx is None:
                idx = len(palette) + 1 if len(palette) < 255 else 0
                palette[key] = idx
            indices.append(idx)

    plte = bytearray(3 * 256)
    for key, idx in palette.items():
        r5, g5, b6 = key
        plte[3 * idx + 0] = (r5 << 3) | (r5 >> 2)
        plte[3 * idx + 1] = (g5 << 3) | (g5 >> 2)
        plte[3 * idx + 2] = (b6 << 2) | (b6 >> 4)
    plte[0:3] = b"\x00\x00\x00"

    raw = bytearray()
    pos = 0
    for _ in range(H):
        raw.append(0)
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
    print("wrote %s (%d bytes, 144x80 palette=%d)" % (out, len(png), len(palette)))


if __name__ == "__main__":
    main()
