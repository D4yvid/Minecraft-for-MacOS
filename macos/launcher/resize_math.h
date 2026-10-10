#pragma once
#include <cmath>
namespace mcfm {
namespace launcher {
// Framebuffer pixels for a view dimension in points; never 0 (the engine divides by it).
inline int pixel_size(double points, double scale) {
  double px = std::floor(points * scale);
  return px < 1 ? 1 : static_cast<int>(px);
}
// A point in an AppKit view (origin bottom-left, points) -> engine pixels (origin top-left).
inline void view_to_pixels(double x_pt, double y_pt, double height_pt, double scale, int *x, int *y) {
  *x = static_cast<int>(std::floor(x_pt * scale));
  *y = static_cast<int>(std::floor((height_pt - y_pt) * scale));
}
}  // namespace launcher
}  // namespace mcfm
