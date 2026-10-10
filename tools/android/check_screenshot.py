#!/usr/bin/env python3
"""check_screenshot.py <frame.ppm>: the frame shows the title screen, not an empty or one-color
buffer: at least 1000 distinct colors, not almost all black, and the MINECRAFT logo's light
stone gray across the top centre (x 25-75%, y 11-28% of the frame), which the sky around it
does not have."""
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

def logo_gray(r, g, b):  # the logo's stone: light, nearly neutral (the sky is clearly blue)
    return r > 150 and g > 150 and b > 150 and max(r, g, b) - min(r, g, b) < 25


x0, x1, y0, y1 = w // 4, 3 * w // 4, h * 11 // 100, h * 28 // 100
region = [(x, y) for y in range(y0, y1) for x in range(x0, x1)]
logo = sum(1 for x, y in region if logo_gray(*px[3 * (y * w + x):3 * (y * w + x) + 3]))
share = 100 * logo // len(region)
print(f"check_screenshot: {w}x{h}, {len(colors)} colors, {100 * dark // (w * h)}% near black, logo gray {share}% of the top centre")
sys.exit(0 if len(colors) >= 1000 and dark < w * h * 0.9 and share >= 15 else 1)
