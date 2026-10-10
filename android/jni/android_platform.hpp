#pragma once
// mcfm::Platform for the Android build of Minecraft PE 0.15.10 (armeabi-v7a): libminecraftpe.so
// keeps its symbols, so everything is found with dlsym, and AppPlatform's vtable is patched
// through mcfm::hook's VirtualTable.
#include <mcfm/platform.h>

#include "hook.hpp"

namespace mcfm {
namespace android {

class AndroidPlatform : public Platform {
 public:
  explicit AndroidPlatform(mcfm::hook::soinfo *minecraftpe);

  // false when this is not the supported game build (AppPlatform vtable symbol missing).
  bool valid() const { return appPlatform_.Valid(); }

  void log(const char *msg);
  bool patch_slot(engine::Slot slot, void *replacement, void **original);
  void *global(engine::Global g);
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy);

 private:
  mcfm::hook::soinfo *minecraftpe_;
  mcfm::hook::VirtualTable appPlatform_;
};

}  // namespace android
}  // namespace mcfm
