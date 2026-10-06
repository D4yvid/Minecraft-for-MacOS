#pragma once
// Desktop keyboard + mouse: puts the game in mouse input mode, reports when it grabs or
// releases the pointer, and feeds key / mouse events into the engine. Platform front ends
// translate their OS events into these calls. C++11.
#include <mcfm/module.h>

namespace mcfm {

struct PointerCallbacks {
  void (*hide)();  // game wants the pointer captured (in-world)
  void (*show)();  // game wants a free cursor (menus)
};

class KeyboardMouseModule : public Module {
 public:
  explicit KeyboardMouseModule(PointerCallbacks callbacks);
  const char *name() const { return "keyboard_mouse"; }
  // Fails when the Keyboard globals or the DefaultInputMode slot are unknown.
  bool init(Platform &platform);

  void key(int vk, bool down);  // Windows virtual-key code 1..255
  void mouse_button(int button, bool down, int x, int y);
  void mouse_move_abs(int x, int y);
  void mouse_move_rel(int dx, int dy);
  void mouse_wheel(int notches, int x, int y);  // sign gives the direction

 private:
  Platform *platform_;
  void *inputs_;
  int32_t *states_;
};

}  // namespace mcfm
