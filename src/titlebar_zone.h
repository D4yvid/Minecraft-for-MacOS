#pragma once

namespace mcpekbm {

// Height of the (hidden) title bar strip in points.
constexpr double kTitleStripPt = 28;

// Window-space points (origin top-left) where hovering reveals the traffic lights.
inline bool in_titlebar_hot_zone(double x, double y) {
  return x >= 0 && y >= 0 && x < 80 && y < 30;
}

// Title bar visibility: revealed by hovering the traffic lights, then kept while the
// pointer stays anywhere in the strip; hidden once it moves below or leaves the window.
struct TitlebarReveal {
  bool shown = false;
  bool update(double x, double y) {
    if (!shown)
      shown = in_titlebar_hot_zone(x, y);
    else if (x < 0 || y < 0 || y >= kTitleStripPt)
      shown = false;
    return shown;
  }
};

// Centre of an AppKit window frame (points, origin bottom-left of the primary display)
// in CoreGraphics global coordinates (origin top-left of the primary display).
inline void window_center_cg(double x, double y, double w, double h, double primaryHeight,
                             double *cx, double *cy) {
  *cx = x + w / 2;
  *cy = primaryHeight - (y + h / 2);
}

}  // namespace mcpekbm
