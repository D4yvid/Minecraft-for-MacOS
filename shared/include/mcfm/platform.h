#pragma once
// What each platform (Apple address table, Android dlsym, ...) provides to the shared
// modules. Keep it small: everything else is built on these four primitives. C++11.
#include <mcfm/engine.h>

namespace mcfm {

class Platform {
 public:
  virtual ~Platform() {}

  virtual void log(const char *msg) = 0;

  // Replace AppPlatform's virtual for `slot` in the live vtable. On success the previous
  // function is stored in *original (when non-null). false: slot unknown on this platform.
  virtual bool patch_slot(engine::Slot slot, void *replacement, void **original) = 0;

  // Address of an engine global, or null when unknown on this platform.
  virtual void *global(engine::Global g) = 0;

  // Mouse::feed: btn per engine::mouse, state 1/0 (wheel: signed delta), x/y absolute
  // pixels, dx/dy relative look motion.
  virtual void mouse_feed(int btn, int state, int x, int y, int dx, int dy) = 0;
};

// printf-style logging through Platform::log.
void logf(Platform &p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace mcfm
