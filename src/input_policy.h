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

// Keys / mouse buttons currently held, as fed to the engine. Only real
// transitions pass; release_all lets focus loss free everything exactly once.
struct HeldSet {
  bool held[256] = {};
  bool set(int code, bool down) {
    if (code <= 0 || code > 255 || held[code] == down) return false;
    held[code] = down;
    return true;
  }
  template <class F> void release_all(F onRelease) {
    for (int c = 1; c < 256; c++)
      if (held[c]) {
        held[c] = false;
        onRelease(c);
      }
  }
};

// While a text field has focus, only Esc goes to the engine (to close chat/entry).
inline bool passes_while_typing(int vk) { return vk == 27; }

// Converts float look deltas to whole engine units, carrying the remainder.
struct DeltaAccumulator {
  float rx = 0, ry = 0;
  void add(float dx, float dy, int *ox, int *oy) {
    rx += dx;
    ry += dy;
    *ox = (int)rx;
    *oy = (int)ry;
    rx -= *ox;
    ry -= *oy;
  }
};

}  // namespace mcpekbm
