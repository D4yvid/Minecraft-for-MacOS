#!/usr/bin/env python3
"""check_screenshot.py [--world] <frame>: the frame (a binary PPM, or a raw `adb exec-out screencap`)
is not an empty or one-color buffer: at least 1000 distinct colors, not almost all black, and

- by default the title screen: the MINECRAFT logo's light stone gray across the top centre
  (x 25-75%, y 11-28% of the frame), which the sky around it does not have;
- with --world a world: no logo there, and the hotbar's dark slots along the bottom centre
  (x 36-67%, y 94-99%; not all of it: a dark launcher wallpaper there is 98%)."""
import struct
import sys


def read_ppm(data):
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    w, h = int(parts[1]), int(parts[2])
    return w, h, 3, data[pos + 1:pos + 1 + w * h * 3]


def read_screencap(data):  # width, height, format (1: RGBA_8888), [color space], pixels
    w, h, fmt = struct.unpack('<III', data[:12])
    assert fmt == 1, 'screencap format %d, not RGBA_8888' % fmt
    return w, h, 4, data[len(data) - w * h * 4:]


args = [a for a in sys.argv[1:] if a != '--world']
world = '--world' in sys.argv[1:]
data = open(args[0], 'rb').read()
w, h, step, px = read_ppm(data) if data[:2] == b'P6' else read_screencap(data)


def at(x, y):
    i = step * (y * w + x)
    return px[i], px[i + 1], px[i + 2]


colors = {px[i:i + 3] for i in range(0, len(px), step)}
dark = sum(1 for i in range(0, len(px), step) if px[i] + px[i + 1] + px[i + 2] < 30)


def share(x0, x1, y0, y1, pred):  # percent of the region's pixels
    region = [(x, y) for y in range(y0, y1) for x in range(x0, x1)]
    return 100 * sum(1 for x, y in region if pred(*at(x, y))) // len(region)


def logo_gray(r, g, b):  # the logo's stone: light, nearly neutral (the sky is clearly blue)
    return r > 150 and g > 150 and b > 150 and max(r, g, b) - min(r, g, b) < 25


def slot_dark(r, g, b):  # the hotbar's slots: dark, nearly neutral
    return max(r, g, b) < 90 and max(r, g, b) - min(r, g, b) < 30


logo = share(w // 4, 3 * w // 4, h * 11 // 100, h * 28 // 100, logo_gray)
hotbar = share(w * 36 // 100, w * 67 // 100, h * 94 // 100, h * 99 // 100, slot_dark)
print(f"check_screenshot: {w}x{h}, {len(colors)} colors, {100 * dark // (w * h)}% near black, "
      f"logo gray {logo}% of the top centre, hotbar dark {hotbar}% of the bottom centre")
ok = len(colors) >= 1000 and dark < w * h * 0.9
ok = ok and (logo < 3 and 10 <= hotbar <= 90 if world else logo >= 15)
sys.exit(0 if ok else 1)
