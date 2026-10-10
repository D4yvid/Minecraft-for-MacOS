#pragma once
// mcfm::Platform for the Mach-O launcher: shared modules (keyboard_mouse, ...) patch our
// AppPlatform vtable and feed the engine's input globals. C++11.
#include <mcfm/platform.h>

#include <cstdint>

namespace mcfm {
namespace launcher {

struct InputAddresses {  // slid
  uintptr_t keyboard_inputs, keyboard_states, keyboard_text;
  uintptr_t mouse_device, mouse_inputs, mouse_inputs_grow, mouse_device_feed;
  static InputAddresses for_slide(uintptr_t slide);
};

class LauncherPlatform : public mcfm::Platform {
 public:
  LauncherPlatform(void **vtable, const InputAddresses &addresses) : vtable_(vtable), a_(addresses) {}
  void log(const char *msg) override;
  bool patch_slot(engine::Slot slot, void *replacement, void **original) override;
  void *global(engine::Global g) override;
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy) override;

 private:
  void **vtable_;
  InputAddresses a_;
};

}  // namespace launcher
}  // namespace mcfm
