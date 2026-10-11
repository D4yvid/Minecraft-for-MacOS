#pragma once
// The iOS app's drawable size (docs/LAUNCHER.md, Stage 4): the view's size in points x the
// screen's native scale, rounded, in landscape (the game runs in landscape only; during a
// rotation the view may still report portrait). Platform-free, C++11.

namespace mcfm {
namespace launcher {

struct PixelSize {
  int w, h;
};

inline PixelSize pixel_size(double w_pt, double h_pt, double scale) {
  int a = static_cast<int>(w_pt * scale + 0.5), b = static_cast<int>(h_pt * scale + 0.5);
  return a >= b ? PixelSize{a, b} : PixelSize{b, a};
}

}  // namespace launcher
}  // namespace mcfm
