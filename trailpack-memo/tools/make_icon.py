#!/usr/bin/env python3
"""Draw the Trailpack Memo icon and embed it.

Writes trailpack-memo/icon.png (64x64 RGBA) and src/generated/EmbeddedIcon.h (the PNG bytes as
TRAILPACK_ICON[]). Pure standard library: shapes are drawn hard-edged at 4x and averaged
down, which anti-aliases them. Optional argument: a path for an extra 256x256 preview PNG.

Motif: a notepad with a spiral binding and a checklist, and a map pin for the trail.
"""
import math
import os
import struct
import sys
import zlib

SIZE = 64
SS = 4                      # supersampling factor
N = SIZE * SS               # drawing canvas (256x256); all coordinates below are in it

OUTLINE = (58, 40, 22, 255)
PAGE = (238, 224, 186, 255)
PAGE_SHADE = (214, 194, 146, 255)
LINE = (150, 120, 80, 255)
CHECK = (74, 170, 74, 255)
PIN = (232, 178, 52, 255)
PIN_DARK = (176, 120, 24, 255)

canvas = [[(0, 0, 0, 0)] * N for _ in range(N)]


def fill(inside, color):
    for y in range(N):
        row = canvas[y]
        for x in range(N):
            if inside(x + 0.5, y + 0.5):
                row[x] = color


def rrect(x0, y0, x1, y1, r):
    def inside(x, y):
        if x < x0 or x > x1 or y < y0 or y > y1:
            return False
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return inside


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def segment(ax, ay, bx, by, w):
    def inside(x, y):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        px, py = ax + t * dx, ay + t * dy
        return (x - px) ** 2 + (y - py) ** 2 <= (w / 2) ** 2
    return inside


def polyline(points, w):
    segs = [segment(*points[i], *points[i + 1], w) for i in range(len(points) - 1)]
    return lambda x, y: any(s(x, y) for s in segs)


def pin(cx, cy, r, tip_y):
    # A circle plus the triangle down to the tip, tangent to the circle.
    d = tip_y - cy
    a = math.asin(r / d)
    tx, ty = r * math.cos(a), r * math.sin(a)   # tangent points relative to the centre
    left, right = (cx - tx, cy + ty), (cx + tx, cy + ty)

    def in_tri(x, y):
        if y < left[1] or y > tip_y:
            return False
        half = (tip_y - y) / (tip_y - left[1]) * tx
        return abs(x - cx) <= half
    c = circle(cx, cy, r)
    return lambda x, y: c(x, y) or in_tri(x, y)


# ── Notepad ───────────────────────────────────────────────────────────────
fill(rrect(36, 30, 196, 236, 22), OUTLINE)
fill(rrect(46, 40, 186, 226, 14), PAGE)
fill(rrect(46, 40, 186, 62, 10), PAGE_SHADE)          # header band under the rings
for rx in (70, 102, 134, 166):                          # spiral rings over the top edge
    fill(rrect(rx - 7, 14, rx + 7, 56, 7), OUTLINE)
    fill(rrect(rx - 3, 20, rx + 3, 50, 3), (110, 90, 64, 255))

# Checklist: two ticked rows, one open.
for i, row_y in enumerate((88, 132, 176)):
    fill(rrect(62, row_y, 94, row_y + 32, 6), OUTLINE)
    fill(rrect(68, row_y + 6, 88, row_y + 26, 3), PAGE)
    fill(rrect(106, row_y + 11, 168 if i < 2 else 140, row_y + 21, 5), LINE)
    if i < 2:
        fill(polyline([(68, row_y + 15), (78, row_y + 26), (100, row_y - 2)], 9), CHECK)

# ── Map pin over the bottom-right corner ──────────────────────────────────
fill(pin(196, 176, 40, 252), OUTLINE)
fill(pin(196, 176, 31, 238), PIN)
fill(circle(196, 176, 31), PIN)
fill(lambda x, y: circle(196, 176, 31)(x, y) and x - 196 > -(y - 176) + 8, PIN_DARK)   # shading
fill(circle(196, 176, 13), OUTLINE)
fill(circle(196, 176, 7), PAGE)


def downsample(src, factor):
    size = len(src) // factor
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = a = 0
            for sy in range(factor):
                for sx in range(factor):
                    pr, pg, pb, pa = src[y * factor + sy][x * factor + sx]
                    r += pr * pa; g += pg * pa; b += pb * pa; a += pa
            n = factor * factor
            row.append((r // a, g // a, b // a, a // n) if a else (0, 0, 0, 0))
        out.append(row)
    return out


def png_bytes(pixels):
    h, w = len(pixels), len(pixels[0])
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
icon = png_bytes(downsample(canvas, SS))
with open(os.path.join(root, "icon.png"), "wb") as f:
    f.write(icon)

lines = ["#pragma once",
         "// Auto-generated from icon.png by tools/make_icon.py (do not edit by hand).",
         "",
         "static const unsigned char TRAILPACK_ICON[] = {"]
for i in range(0, len(icon), 16):
    lines.append("    " + ", ".join("0x%02x" % b for b in icon[i:i + 16]) + ",")
lines += ["};", "static const unsigned int TRAILPACK_ICON_size = %d;" % len(icon), ""]
with open(os.path.join(root, "src", "generated", "EmbeddedIcon.h"), "w") as f:
    f.write("\n".join(lines))

if len(sys.argv) > 1:
    with open(sys.argv[1], "wb") as f:
        f.write(png_bytes(canvas))
print("icon.png: %d bytes" % len(icon))
