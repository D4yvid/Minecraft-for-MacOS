// Android entry point: loaded by MainActivity (System.loadLibrary("mcfm")), it attaches to
// libminecraftpe.so and installs the shared hardcoded Win10 (desktop) UI.
#include <dlfcn.h>
#include <jni.h>

#include <mcfm/win10_ui.h>

#include "android_platform.hpp"
#include "log.hpp"

namespace {

void init() {
  // Only dlsym is used on the handle, so the plain (possibly opaque) dlopen handle is fine.
  mcfm::hook::soinfo *minecraftpe = (mcfm::hook::soinfo *)::dlopen("libminecraftpe.so", RTLD_NOW);
  if (!minecraftpe) {
    LOGE("couldn't open libminecraftpe.so: %s", dlerror());
    return;
  }
  // Function-local static: constructed on first use, never before the C++ runtime is up.
  static mcfm::android::AndroidPlatform platform(minecraftpe);
  if (!platform.valid()) {
    platform.log("not Minecraft PE 0.15.10 (no AppPlatform_android23 vtable), disabled");
    return;
  }
  bool ok = mcfm::win10_ui::install(platform);
  platform.log(ok ? "patched" : "patched (some features failed)");
}

}  // namespace

extern "C" jint JNI_OnLoad(JavaVM *, void *) {
  init();
  return JNI_VERSION_1_2;
}
