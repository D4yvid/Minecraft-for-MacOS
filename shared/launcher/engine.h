#pragma once
// Boots the game engine the way minecraftpeViewController does on iOS (awakeFromNib,
// initView, drawFrame), with our AppPlatform instead of AppPlatform_iOS. C++11.
#include <cstdint>

#include "app_platform.h"

namespace mcfm {
namespace launcher {

// Slid addresses of the engine functions the boot sequence calls.
struct EngineAddresses {
  uintptr_t platform_ctor, base_vtable, client_ctor, app_init;
  uintptr_t graphics_vendor, graphics_renderer, graphics_version, graphics_extensions;
  uintptr_t fire_suspended, fire_resumed, fire_focus_lost, fire_focus_gained;  // AppPlatform lifecycle
  static EngineAddresses for_slide(uintptr_t slide);  // addresses_0_15_10.h + slide
};

class Engine {
 public:
  // Needs a current GL context. Sizes are framebuffer pixels.
  bool start(const EngineAddresses &addresses, const HostInfo &info, int width, int height);
  void frame();                         // App::update()
  void resize(int width, int height);   // setRenderingSize + setUISizeAndScale
  // App lifecycle (main thread), as iOS: suspend before quitting (the game saves), focus
  // lost/gained when the app is switched away from / back to.
  void suspend();
  void resume();
  void focus_lost();
  void focus_gained();
  void *app() const { return app_; }
  void *platform() const { return platform_; }
  void **vtable() { return vtable_; }    // our AppPlatform vtable (LauncherPlatform patches it)

 private:
  void fire(uintptr_t notifier);
  EngineAddresses a_ = {};
  void *platform_ = nullptr, *context_ = nullptr, *app_ = nullptr;
  void *vtable_[kBaseSlots];
};

}  // namespace launcher
}  // namespace mcfm
