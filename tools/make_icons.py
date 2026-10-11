#!/usr/bin/env python3
"""Makes the app icons, OpenA2Racer.icns (macOS) and OpenA2Racer.ico
(Windows), from the square icon PNG.

The source (assets/OpenA2Racer_icon.png) is a rounded square that fills the
whole image, with white outside its corners. The icons are transparent there.
This cuts the corners away along the same curve; for macOS it also leaves the
margin of its icon grid (the body is 824 of 1024 pixels). sips and iconutil
write the sizes.

Usage: tools/make_icons.py assets/OpenA2Racer_icon.png assets
Needs only Python 3 and macOS's sips and iconutil.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib


def read_png(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind, chunk = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
            if depth != 8 or colour not in (2, 6):
                sys.exit("Expected an 8-bit RGB or RGBA PNG.")
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        previous = line
    return width, height, channels, rows


def write_png(path, size, pixels):
    raw = b"".join(b"\0" + bytes(pixels[y * size * 4:(y + 1) * size * 4]) for y in range(size))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
                  chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def corner_radius(width, channels, rows):
    """Where the top edge stops being white: the radius of the corners."""
    middle = rows[0]
    for x in range(width // 2):
        r, g, b = middle[x * channels:x * channels + 3]
        if r > 128 and g < 128:
            return x
    return 0


def render(width, height, channels, rows, radius, canvas, body):
    """The icon on a transparent `canvas`-pixel square, its body `body` wide."""
    margin = (canvas - body) / 2
    scale = width / body
    # The corner curve, a little inside the source's so no white shows.
    r = radius / scale - 1.0
    pixels = bytearray(canvas * canvas * 4)
    samples = 4
    for y in range(canvas):
        for x in range(canvas):
            red = green = blue = cover = 0.0
            for sy in range(samples):
                for sx in range(samples):
                    bx = x + (sx + 0.5) / samples - margin
                    by = y + (sy + 0.5) / samples - margin
                    if not (0 <= bx < body and 0 <= by < body):
                        continue
                    cx = min(max(bx, r), body - r)
                    cy = min(max(by, r), body - r)
                    if math.hypot(bx - cx, by - cy) > r:
                        continue
                    row = rows[min(int(by * scale), height - 1)]
                    i = min(int(bx * scale), width - 1) * channels
                    red += row[i]
                    green += row[i + 1]
                    blue += row[i + 2]
                    cover += 1
            if cover:
                o = (y * canvas + x) * 4
                pixels[o:o + 4] = bytes((round(red / cover), round(green / cover), round(blue / cover),
                                         round(255 * cover / samples ** 2)))
    return pixels


def resize(master, size, out):
    subprocess.run(["sips", "-z", str(size), str(size), master, "--out", out], check=True,
                   stdout=subprocess.DEVNULL)


def write_ico(path, images):
    """An .ico with PNG-compressed images (Windows Vista and later)."""
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, png in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    with open(path, "wb") as out:
        out.write(header + entries + data)


def main():
    source, folder = sys.argv[1], sys.argv[2]
    width, height, channels, rows = read_png(source)
    radius = corner_radius(width, channels, rows)
    with tempfile.TemporaryDirectory() as work:
        # macOS: the 1024 grid with its margin, every size in an .icns.
        master = os.path.join(work, "macos.png")
        write_png(master, 1024, render(width, height, channels, rows, radius, 1024, 824))
        iconset = os.path.join(work, "icon.iconset")
        os.mkdir(iconset)
        for size in (16, 32, 128, 256, 512):
            for factor, suffix in ((1, ""), (2, "@2x")):
                resize(master, size * factor, os.path.join(iconset, f"icon_{size}x{size}{suffix}.png"))
        icns = os.path.join(folder, "OpenA2Racer.icns")
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)

        # Windows: the body fills the square.
        master = os.path.join(work, "windows.png")
        write_png(master, 256, render(width, height, channels, rows, radius, 256, 256))
        images = []
        for size in (16, 24, 32, 48, 64, 128, 256):
            out = os.path.join(work, f"windows-{size}.png")
            resize(master, size, out)
            images.append((size, open(out, "rb").read()))
        ico = os.path.join(folder, "OpenA2Racer.ico")
        write_ico(ico, images)
    print(f"{icns}, {ico}: corner radius {radius} of {width}")


if __name__ == "__main__":
    main()
