#pragma once
// mcfm::Platform for the Android build of Minecraft PE 0.15.10 (armeabi-v7a): libminecraftpe.so
// keeps its symbols, so everything is found with dlsym, and AppPlatform's vtable is patched
// through runet's VirtualTable.
#include <mcfm/platform.h>

#include "hook.hpp"

namespace mcfm {
namespace android {

class AndroidPlatform : public Platform {
 public:
  explicit AndroidPlatform(runet::hook::soinfo *minecraftpe);

  // false when this is not the supported game build (AppPlatform vtable symbol missing).
  bool valid() const { return appPlatform_.Valid(); }

  void log(const char *msg);
  bool patch_slot(engine::Slot slot, void *replacement, void **original);
  void *global(engine::Global g);
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy);

 private:
  runet::hook::soinfo *minecraftpe_;
  runet::hook::VirtualTable appPlatform_;
};

// Makes libminecraftpe.so's mappings writable (vtables live in read-only relro).
bool make_engine_writable();

}  // namespace android
}  // namespace mcfm
