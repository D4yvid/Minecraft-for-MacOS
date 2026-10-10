#pragma once
// Mouse look and hotbar scrolling from AppKit events (macOS launcher).
#include <cmath>

namespace mcfm {
namespace launcher {

// Look motion: AppKit deltas are fractional points; the engine takes whole pixels. Scale to
// pixels and carry the remainder, so slow, small movements still turn the camera.
class LookConverter {
 public:
  explicit LookConverter(double scale) : scale_(scale) {}
  void set_scale(double scale) { scale_ = scale; }
  void feed(double dx_pt, double dy_pt, int *dx, int *dy) {
    rx_ += dx_pt * scale_;
    ry_ += dy_pt * scale_;
    *dx = whole(rx_);
    *dy = whole(ry_);
    rx_ -= *dx;
    ry_ -= *dy;
  }

 private:
  // Whole part, tolerating float drift (ten steps of 0.6 must make 6, not 5.999...).
  static int whole(double v) { return static_cast<int>(std::trunc(v + std::copysign(1e-9, v))); }
  double scale_, rx_ = 0, ry_ = 0;
};

// Hotbar steps. A mouse wheel click is one step, as on Windows. Trackpad (precise) scrolling
// accumulates points per step, drops the remainder on a direction change or a pause, and
// gives at most one step per kTrackpadMinGap so momentum cannot spin the hotbar.
class HotbarScroll {
 public:
  static constexpr double kTrackpadStep = 12.0;     // points of trackpad scrolling per step
  static constexpr double kTrackpadMinGap = 0.05;   // seconds between trackpad steps
  static constexpr double kTrackpadIdle = 0.3;      // a pause this long starts over

  int feed(double delta, bool precise, double now) {
    if (!precise) return delta > 0 ? 1 : (delta < 0 ? -1 : 0);
    if (now - last_event_ > kTrackpadIdle || (delta > 0 && acc_ < 0) || (delta < 0 && acc_ > 0)) acc_ = 0;
    last_event_ = now;
    acc_ += delta;
    if (std::fabs(acc_) < kTrackpadStep) return 0;
    if (now - last_step_ < kTrackpadMinGap) {
      acc_ = acc_ > 0 ? kTrackpadStep : -kTrackpadStep;  // hold one step for a later event
      return 0;
    }
    int step = acc_ > 0 ? 1 : -1;
    acc_ -= step * kTrackpadStep;
    if (std::fabs(acc_) >= kTrackpadStep) acc_ = 0;  // big jumps: one step per event
    last_step_ = now;
    return step;
  }

 private:
  double acc_ = 0, last_event_ = -1e9, last_step_ = -1e9;
};

}  // namespace launcher
}  // namespace mcfm
