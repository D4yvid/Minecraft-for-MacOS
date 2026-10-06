#pragma once
// Test double for mcfm::Platform: an in-memory "engine" with a fake AppPlatform vtable,
// Keyboard globals and a recorded mouse stream. C++11.
#include <mcfm/platform.h>

#include <map>
#include <string>
#include <vector>

struct MouseCall { int btn, state, x, y, dx, dy; };

class FakePlatform : public mcfm::Platform {
 public:
  std::vector<std::string> logs;
  std::map<int, void *> slots;            // current vtable contents, by Slot
  std::vector<mcfm::engine::KeyEvent> keyboardInputs;
  int32_t keyboardStates[256];
  std::vector<MouseCall> mouse;
  bool hasGlobals;

  FakePlatform() : hasGlobals(true) {
    for (int i = 0; i < 256; i++) keyboardStates[i] = 0;
  }
  void set_slot(mcfm::engine::Slot s, void *fn) { slots[(int)s] = fn; }
  void *slot(mcfm::engine::Slot s) { return slots.count((int)s) ? slots[(int)s] : 0; }
  bool logged(const std::string &needle) const {
    for (size_t i = 0; i < logs.size(); i++)
      if (logs[i].find(needle) != std::string::npos) return true;
    return false;
  }

  void log(const char *msg) { logs.push_back(msg); }
  bool patch_slot(mcfm::engine::Slot s, void *replacement, void **original) {
    if (!slots.count((int)s)) return false;
    if (original) *original = slots[(int)s];
    slots[(int)s] = replacement;
    return true;
  }
  void *global(mcfm::engine::Global g) {
    if (!hasGlobals) return 0;
    if (g == mcfm::engine::Global::KeyboardInputs) return &keyboardInputs;
    if (g == mcfm::engine::Global::KeyboardStates) return keyboardStates;
    return 0;
  }
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy) {
    MouseCall c = {btn, state, x, y, dx, dy};
    mouse.push_back(c);
  }
};
