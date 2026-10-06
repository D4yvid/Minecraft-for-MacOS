#pragma once
// Minecraft PE 0.15.10 engine vocabulary, the same on every platform. C++11.
#include <stdint.h>

namespace mcfm {
namespace engine {

// AppPlatform virtual functions a module may replace.
enum class Slot {
  GetEdition,        // std::string () const — "pocket" / "win10"
  DefaultInputMode,  // int () — 1 mouse, 2 touch, 3 gamepad
  UIScalingRules,    // int () — 0 desktop, 1-2 pocket
  UseCenteredGUI,    // bool () const — true on desktop
  HideMousePointer,  // void () — game grabs the mouse (in-world)
  ShowMousePointer,  // void () — game releases the mouse (menus)
};

// Engine globals.
enum class Global {
  KeyboardInputs,  // std::vector<KeyEvent>, drained by the game every tick
  KeyboardStates,  // int32_t[256], current key state by key code
};

// One entry of Keyboard::_inputs. Identical layout on armv7 and arm64.
struct KeyEvent {
  int32_t state;  // 1 down, 0 up
  uint8_t key;    // Windows virtual-key code
  uint8_t pad[3];
};
static_assert(sizeof(KeyEvent) == 8, "KeyEvent must match the engine");

// Mouse::feed button codes.
namespace mouse {
enum { Move = 0, Left = 1, Right = 2, Middle = 3, Wheel = 4 };
}

}  // namespace engine
}  // namespace mcfm
