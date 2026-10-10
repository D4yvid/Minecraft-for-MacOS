#pragma once
#include <cmath>
namespace mcfm {
namespace launcher {
// Framebuffer pixels for a view dimension in points; never 0 (the engine divides by it).
inline int pixel_size(double points, double scale) {
  double px = std::floor(points * scale);
  return px < 1 ? 1 : static_cast<int>(px);
}
}  // namespace launcher
}  // namespace mcfm
