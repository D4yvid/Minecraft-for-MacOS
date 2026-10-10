#pragma once
// Writes a frame read back with glReadPixels (RGBA, rows bottom-up) as a binary PPM (P6,
// RGB, rows top-down). Used by mcfm-launch --screenshot.
#include <cstdio>

namespace mcfm {
namespace launcher {

inline bool write_ppm(const char *path, int width, int height, const unsigned char *rgba) {
  std::FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", width, height);
  bool ok = true;
  for (int y = height - 1; y >= 0 && ok; y--) {
    const unsigned char *row = rgba + static_cast<long>(y) * width * 4;
    for (int x = 0; x < width && ok; x++) ok = std::fwrite(row + 4 * x, 1, 3, f) == 3;
  }
  return std::fclose(f) == 0 && ok;
}

}  // namespace launcher
}  // namespace mcfm
