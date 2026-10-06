#pragma once

namespace mcpekbm {

enum class PointerEvent { Move, ButtonDown, ButtonUp, Scroll };

struct PointerState {
  bool captured;  // game has grabbed the mouse (in-world)
  bool inside;    // free cursor is over the game view
  double xPt, yPt;  // free cursor position in window points
};

// Height of the (hidden) title bar strip: clicks there drag the window, not play.
constexpr double kTitleStripPt = 28;

// Whether a Mac pointer event should be forwarded to the engine.
inline bool should_feed(PointerEvent e, const PointerState &s) {
  if (e == PointerEvent::ButtonUp) return true;  // never leave a button held
  if (s.captured) return e != PointerEvent::Move;  // look uses relative deltas
  if (!s.inside) return false;
  if (e == PointerEvent::Move) return true;
  return s.yPt >= kTitleStripPt;  // ButtonDown / Scroll on the window chrome
}

// UIKit also ends hover when a button goes down or the pointer moves onto AppKit's
// traffic lights. Only an end at the view's edge means the pointer really left.
inline bool hover_end_is_exit(double xPt, double yPt, double widthPt, double heightPt) {
  constexpr double kEdge = 4;
  return xPt < kEdge || yPt < kEdge || xPt > widthPt - kEdge || yPt > heightPt - kEdge;
}

}  // namespace mcpekbm
