#include "android_platform.hpp"

#include <android/log.h>
#include <dlfcn.h>

namespace mcfm {
namespace android {
namespace {

// The AppPlatform implementation the game instantiates on Android 6+.
const char *kAppPlatformVtable = "_ZTV21AppPlatform_android23";

// Symbol of the function currently in each slot; its vtable index is found by value.
const char *slot_symbol(engine::Slot s) {
  switch (s) {
    case engine::Slot::GetEdition: return "_ZNK11AppPlatform10getEditionEv";
    case engine::Slot::UIScalingRules: return "_ZNK19AppPlatform_android25getPlatformUIScalingRulesEv";
    case engine::Slot::UseCenteredGUI: return "_ZNK11AppPlatform14useCenteredGUIEv";
    case engine::Slot::PlatformType: return "_ZNK11AppPlatform15getPlatformTypeEv";
    case engine::Slot::UseMetadataDrivenScreens: return "_ZNK19AppPlatform_android24useMetadataDrivenScreensEv";
    case engine::Slot::HideMousePointer: return "_ZN11AppPlatform16hideMousePointerEv";
    case engine::Slot::ShowMousePointer: return "_ZN11AppPlatform16showMousePointerEv";
    case engine::Slot::DefaultInputMode: return 0;  // not exported under a known name
  }
  return 0;
}

}  // namespace

AndroidPlatform::AndroidPlatform(runet::hook::soinfo *minecraftpe)
    : minecraftpe_(minecraftpe), appPlatform_(minecraftpe, kAppPlatformVtable) {}

void AndroidPlatform::log(const char *msg) { __android_log_print(ANDROID_LOG_INFO, "mcfm", "%s", msg); }

bool AndroidPlatform::patch_slot(engine::Slot slot, void *replacement, void **original) {
  const char *symbol = slot_symbol(slot);
  if (!symbol) return false;
  int index = appPlatform_.FindIndex(symbol);
  if (index < 0) return false;
  return appPlatform_.Hook(index, replacement, original);
}

void *AndroidPlatform::global(engine::Global g) {
  switch (g) {
    case engine::Global::KeyboardInputs: return ::dlsym(minecraftpe_, "_ZN8Keyboard7_inputsE");
    case engine::Global::KeyboardStates: return ::dlsym(minecraftpe_, "_ZN8Keyboard7_statesE");
  }
  return 0;
}

void AndroidPlatform::mouse_feed(int btn, int state, int x, int y, int dx, int dy) {
  typedef void (*MouseFeed)(char, char, short, short, short, short);
  static MouseFeed feed = (MouseFeed)::dlsym(minecraftpe_, "_ZN5Mouse4feedEccssss");
  if (feed) feed((char)btn, (char)state, (short)x, (short)y, (short)dx, (short)dy);
}

}  // namespace android
}  // namespace mcfm
