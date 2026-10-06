#include <mcfm/keyboard_mouse.h>

#include <cstring>
#include <new>

namespace mcfm {
namespace keyboard_mouse {
namespace {

PointerCallbacks gCallbacks = {0, 0};
Platform *gPlatform = 0;  // set once install() succeeds
void *gInputs = 0;
int32_t *gStates = 0;

int default_input_mode(void *) { return 1; }  // 1 mouse, 2 touch, 3 gamepad
void hide_mouse_pointer(void *) { if (gCallbacks.hide) gCallbacks.hide(); }
void show_mouse_pointer(void *) { if (gCallbacks.show) gCallbacks.show(); }

// std::vector<T> as both libc++ and gnustl lay it out: begin / end / end-of-storage.
template <class T> struct RawVector { T *begin, *end, *cap; };

// push_back done here so it works whatever the engine's STL. Growth uses the global
// operator new/delete, which is what std::allocator uses in the engine too.
void push(RawVector<engine::KeyEvent> *v, const engine::KeyEvent &e) {
  if (v->end == v->cap) {
    size_t size = v->end - v->begin;
    size_t cap = size < 8 ? 16 : size * 2;
    engine::KeyEvent *buf = static_cast<engine::KeyEvent *>(::operator new(cap * sizeof(engine::KeyEvent)));
    if (size) std::memcpy(buf, v->begin, size * sizeof(engine::KeyEvent));
    ::operator delete(v->begin);
    v->begin = buf;
    v->end = buf + size;
    v->cap = buf + cap;
  }
  *v->end = e;
  v->end++;
}

}  // namespace

bool install(Platform &p, PointerCallbacks callbacks) {
  gPlatform = 0;  // until this install succeeds
  gCallbacks = callbacks;
  gInputs = p.global(engine::Global::KeyboardInputs);
  gStates = static_cast<int32_t *>(p.global(engine::Global::KeyboardStates));
  if (!gInputs || !gStates) {
    p.log("keyboard_mouse: Keyboard globals unknown on this platform");
    return false;
  }
  if (!p.patch_slot(engine::Slot::DefaultInputMode, (void *)&default_input_mode, 0)) {
    p.log("keyboard_mouse: DefaultInputMode unknown on this platform");
    return false;
  }
  if (!p.patch_slot(engine::Slot::HideMousePointer, (void *)&hide_mouse_pointer, 0) ||
      !p.patch_slot(engine::Slot::ShowMousePointer, (void *)&show_mouse_pointer, 0))
    p.log("keyboard_mouse: pointer hide/show unknown, no capture");
  gPlatform = &p;
  p.log("keyboard_mouse: installed");
  return true;
}

void key(int vk, bool down) {
  if (!gPlatform || vk <= 0 || vk > 255) return;
  engine::KeyEvent e;
  std::memset(&e, 0, sizeof e);
  e.state = down ? 1 : 0;
  e.key = static_cast<uint8_t>(vk);
  push(static_cast<RawVector<engine::KeyEvent> *>(gInputs), e);
  gStates[vk] = e.state;
}

void mouse_button(int button, bool down, int x, int y) {
  if (gPlatform) gPlatform->mouse_feed(button, down ? 1 : 0, x, y, 0, 0);
}

void mouse_move_abs(int x, int y) {
  if (gPlatform) gPlatform->mouse_feed(engine::mouse::Move, 0, x, y, 0, 0);
}

void mouse_move_rel(int dx, int dy) {
  if (gPlatform && (dx || dy)) gPlatform->mouse_feed(engine::mouse::Move, 0, 0, 0, dx, dy);
}

void mouse_wheel(int notches, int x, int y) {
  if (gPlatform && notches) gPlatform->mouse_feed(engine::mouse::Wheel, notches > 0 ? 127 : -127, x, y, 0, 0);
}

}  // namespace keyboard_mouse
}  // namespace mcfm
