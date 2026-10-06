// Android entry point: loaded by MainActivity (System.loadLibrary("runet")), it attaches to
// libminecraftpe.so and installs the shared hardcoded Win10 (desktop) UI.
#include <jni.h>

#include <mcfm/win10_ui.h>

#include "android.hpp"
#include "android_platform.hpp"
#include "hook.hpp"
#include "jni.hpp"
#include "log.hpp"

namespace {

void init(JavaVM *vm) {
  if (!runet::android::InitializeFunctions()) {
    LOGE("couldn't initialise android functions");
    return;
  }
  runet::jni::JavaEnviroment *java = new runet::jni::JavaEnviroment(vm);
  java->SetCurrentJavaEnviroment();
  if (!runet::hook::InitializeBaseHooking()) {
    LOGE("couldn't initialise hooking");
    return;
  }
  runet::hook::soinfo *minecraftpe = runet::hook::LoadLibrary("libminecraftpe.so", RTLD_LAZY | RTLD_NOW);
  if (!minecraftpe) {
    LOGE("couldn't open libminecraftpe.so");
    return;
  }
  // Function-local static: constructed on first use, never before the C++ runtime is up.
  static mcfm::android::AndroidPlatform platform(minecraftpe);
  if (!mcfm::android::make_engine_writable()) {
    platform.log("couldn't make libminecraftpe.so writable, disabled");
    return;
  }
  bool ok = mcfm::win10_ui::install(platform);
  platform.log(ok ? "patched" : "patched (some features failed)");
}

}  // namespace

extern "C" jint JNI_OnLoad(JavaVM *vm, void *) {
  init(vm);
  return JNI_VERSION_1_2;
}

// Kept so APKs patched for runet-client (which call runetOnCreate) still link.
extern "C" void Java_com_mojang_minecraftpe_MainActivity_runetOnCreate(JNIEnv *, jobject) {}
