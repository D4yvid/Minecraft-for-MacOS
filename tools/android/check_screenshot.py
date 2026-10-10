#!/usr/bin/env python3
"""check_screenshot.py <frame.ppm>: the frame shows a rendered screen, not an empty or one-color
buffer: at least 1000 distinct colors, and neither almost all black nor all one color."""
import sys


def read_ppm(path):
    data = open(path, 'rb').read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    assert parts[0] == b'P6', 'not a binary PPM'
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


w, h, px = read_ppm(sys.argv[1])
colors = {px[i:i + 3] for i in range(0, len(px), 3)}
dark = sum(1 for i in range(0, len(px), 3) if px[i] + px[i + 1] + px[i + 2] < 30)
print(f"check_screenshot: {w}x{h}, {len(colors)} colors, {100 * dark // (w * h)}% near black")
sys.exit(0 if len(colors) >= 1000 and dark < w * h * 0.9 else 1)
