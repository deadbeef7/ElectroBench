#!/usr/bin/env python3
"""Convert a binary P6 PPM (what the scenes' WriteScreenshotPPM emits) to PNG.

    python3 android/ppm_to_png.py IN.ppm OUT.png

Pure Python and zlib only: the port's screenshot path is deliberately
dependency-free (the Vita build had no image library either), and the harness
output should stay inspectable on any machine that can run the Makefile.
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # Header: P6, then whitespace-separated width, height and maxval, each
    # optionally preceded by #-comments.
    fields = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i : i + 1] not in (b"\n", b"\r"):
                i += 1
            continue
        start = i
        while i < len(data) and not data[i : i + 1].isspace():
            i += 1
        fields.append(int(data[start:i]))
    i += 1  # single whitespace byte after maxval
    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit("only 8-bit PPMs are supported (maxval=%d)" % maxval)
    pixels = data[i:]
    if len(pixels) < width * height * 3:
        raise SystemExit("truncated PPM: %d bytes for %dx%d" % (len(pixels), width, height))
    return width, height, pixels[: width * height * 3]


def write_png(path, width, height, rgb):
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type 0
        row = rgb[y * width * 3 : (y + 1) * width * 3]
        for x in range(width):
            rows += row[x * 3 : x * 3 + 3]
            rows.append(255)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ppm_to_png.py IN.ppm OUT.png")
    width, height, rgb = read_ppm(sys.argv[1])
    write_png(sys.argv[2], width, height, rgb)
    lit = sum(1 for i in range(0, len(rgb), 3) if rgb[i] + rgb[i + 1] + rgb[i + 2] > 24)
    print("wrote %s (%dx%d, lit=%d/%d)" % (sys.argv[2], width, height, lit, width * height))


if __name__ == "__main__":
    main()
