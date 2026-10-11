#pragma once
// The iOS app's drawable size (docs/LAUNCHER.md, Stage 4): the view's size in points x the
// screen's native scale, rounded, in the view's own orientation. Never swapped: ANGLE's Metal
// window surface (WindowSurfaceMtl::checkIfLayerResized) sets the layer's drawableSize to its
// bounds x contentsScale before each frame, so any other shape would be stretched onto the layer
// (or replaced a frame later). The app is landscape-only (Info.plist, the view controller), so the
// view is landscape. Platform-free, C++11.

namespace mcfm {
namespace launcher {

struct PixelSize {
  int w, h;
};

inline PixelSize pixel_size(double w_pt, double h_pt, double scale) {
  return PixelSize{static_cast<int>(w_pt * scale + 0.5), static_cast<int>(h_pt * scale + 0.5)};
}

}  // namespace launcher
}  // namespace mcfm
