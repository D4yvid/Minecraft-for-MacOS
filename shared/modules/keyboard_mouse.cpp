#include <mcfm/modules/keyboard_mouse.h>

#include <cstring>
#include <new>

namespace mcfm {
namespace {

PointerCallbacks gCallbacks = {0, 0};

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

KeyboardMouseModule::KeyboardMouseModule(PointerCallbacks callbacks)
    : platform_(0), inputs_(0), states_(0) {
  gCallbacks = callbacks;
}

bool KeyboardMouseModule::init(Platform &p) {
  inputs_ = p.global(engine::Global::KeyboardInputs);
  states_ = static_cast<int32_t *>(p.global(engine::Global::KeyboardStates));
  if (!inputs_ || !states_) {
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
  platform_ = &p;
  return true;
}

void KeyboardMouseModule::key(int vk, bool down) {
  if (!platform_ || vk <= 0 || vk > 255) return;
  engine::KeyEvent e;
  std::memset(&e, 0, sizeof e);
  e.state = down ? 1 : 0;
  e.key = static_cast<uint8_t>(vk);
  push(static_cast<RawVector<engine::KeyEvent> *>(inputs_), e);
  states_[vk] = e.state;
}

void KeyboardMouseModule::mouse_button(int button, bool down, int x, int y) {
  if (platform_) platform_->mouse_feed(button, down ? 1 : 0, x, y, 0, 0);
}

void KeyboardMouseModule::mouse_move_abs(int x, int y) {
  if (platform_) platform_->mouse_feed(engine::mouse::Move, 0, x, y, 0, 0);
}

void KeyboardMouseModule::mouse_move_rel(int dx, int dy) {
  if (platform_ && (dx || dy)) platform_->mouse_feed(engine::mouse::Move, 0, 0, 0, dx, dy);
}

void KeyboardMouseModule::mouse_wheel(int notches, int x, int y) {
  if (platform_ && notches) platform_->mouse_feed(engine::mouse::Wheel, notches > 0 ? 127 : -127, x, y, 0, 0);
}

}  // namespace mcfm
