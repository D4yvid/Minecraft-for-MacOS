#pragma once
// Feeding Minecraft PE 0.15.10's mouse (Catalyst AddressPlatform and the launcher). C++11.
#include <cstdint>

namespace mcfm {
namespace apple {

struct MouseAction {  // MouseDevice::_inputs element
  int16_t x, y, dx, dy;
  int8_t button, data;
  uint8_t pad[6];
};
static_assert(sizeof(MouseAction) == 16, "MouseAction layout");

inline int16_t clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : static_cast<int16_t>(v)); }

// Relative motion goes straight into Mouse::_instance's queue (MouseDevice::feed in this
// build has no dx/dy; the engine's slow path grows the vector); everything else through feed.
inline void feed_mouse(uintptr_t inputs, uintptr_t grow, uintptr_t device, uintptr_t device_feed,
                       int btn, int state, int x, int y, int dx, int dy) {
  if (dx || dy) {
    MouseAction a = {};
    a.dx = clamp16(dx);
    a.dy = clamp16(dy);
    struct Vec { MouseAction *begin, *end, *cap; };
    Vec *v = reinterpret_cast<Vec *>(inputs);
    if (v->end < v->cap) {
      *v->end = a;
      v->end++;
    } else {
      reinterpret_cast<void (*)(void *, MouseAction *)>(grow)(v, &a);
    }
    return;
  }
  reinterpret_cast<void (*)(void *, int, int, int, int)>(device_feed)(reinterpret_cast<void *>(device), btn, state,
                                                                      clamp16(x), clamp16(y));
}

}  // namespace apple
}  // namespace mcfm
