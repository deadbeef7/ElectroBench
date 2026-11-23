#!/usr/bin/env python3
"""Convert a P6 PPM frame to an 8-bit RGBA PNG.

The host GLES3 check (android/Makefile's host-render) writes P6 PPMs, because
that is the only format the scenes' own screenshot path knows. The store listing
wants PNG, so the PNG is written through buildtools/make_icons.write_rgba -- the
same stdlib-only writer (no Pillow) the launcher icons already go through.

    python3 buildtools/ppm_to_png.py IN.ppm OUT.png
"""
import sys

import make_icons


def _token(data, pos):
    """Next whitespace-separated header token, skipping '#' comments."""
    while True:
        while pos < len(data) and data[pos] in b" \t\r\n":
            pos += 1
        if pos < len(data) and data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos] not in b"\r\n":
                pos += 1
        else:
            break
    start = pos
    while pos < len(data) and data[pos] not in b" \t\r\n":
        pos += 1
    return data[start:pos], pos


def read_ppm(path):
    """Return (w, h, rgb) for a binary 8-bit PPM, top row first."""
    with open(path, "rb") as f:
        data = f.read()
    magic, pos = _token(data, 0)
    if magic != b"P6":
        raise ValueError("%s: not a binary P6 PPM (got %r)" % (path, magic))
    width, pos = _token(data, pos)
    height, pos = _token(data, pos)
    maxval, pos = _token(data, pos)
    w, h, top = int(width), int(height), int(maxval)
    if top != 255:
        raise ValueError("%s: only 8-bit PPMs are supported (maxval %d)" % (path, top))
    pos += 1  # the single whitespace byte that ends the header
    rgb = data[pos:pos + w * h * 3]
    if len(rgb) != w * h * 3:
        raise ValueError("%s: expected %d pixel bytes, found %d" % (path, w * h * 3, len(rgb)))
    return w, h, rgb


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: ppm_to_png.py IN.ppm OUT.png\n")
        return 2
    w, h, rgb = read_ppm(sys.argv[1])
    rgba = bytearray(w * h * 4)
    rgba[0::4] = rgb[0::3]
    rgba[1::4] = rgb[1::3]
    rgba[2::4] = rgb[2::3]
    rgba[3::4] = b"\xff" * (w * h)
    size = make_icons.write_rgba(sys.argv[2], w, h, rgba)
    print("%s: %dx%d RGBA, %d bytes" % (sys.argv[2], w, h, size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
