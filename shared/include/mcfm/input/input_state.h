#pragma once

// Platform-neutral input bookkeeping shared by every front end. C++11.

namespace mcfm {

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

}  // namespace mcfm
