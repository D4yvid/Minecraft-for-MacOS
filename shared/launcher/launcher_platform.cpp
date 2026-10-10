#include "launcher_platform.h"

#include <cstdio>

#include "addresses_0_15_10.h"
#include "engine_mouse.h"

namespace mcfm {
namespace launcher {
namespace {

int slot_index(engine::Slot s) {  // docs/research/appplatform-vtable.md
  switch (s) {
    case engine::Slot::GetEdition: return 93;
    case engine::Slot::DefaultInputMode: return 96;
    case engine::Slot::UIScalingRules: return 99;
    case engine::Slot::UseCenteredGUI: return 68;
    case engine::Slot::PlatformType: return 69;
    case engine::Slot::UseMetadataDrivenScreens: return 66;
    case engine::Slot::HideMousePointer: return 13;
    case engine::Slot::ShowMousePointer: return 14;
  }
  return -1;
}

}  // namespace

InputAddresses InputAddresses::for_slide(uintptr_t slide) {
  InputAddresses a;
  a.keyboard_inputs = addr::kKeyboardInputs + slide;
  a.keyboard_states = addr::kKeyboardStates + slide;
  a.keyboard_text = addr::kKeyboardText + slide;
  a.mouse_device = addr::kMouseDevice + slide;
  a.mouse_inputs = addr::kMouseInputs + slide;
  a.mouse_inputs_grow = addr::kMouseInputsGrow + slide;
  a.mouse_device_feed = addr::kMouseDeviceFeed + slide;
  return a;
}

void LauncherPlatform::log(const char *msg) { std::fprintf(stderr, "mcfm: %s\n", msg); }

bool LauncherPlatform::patch_slot(engine::Slot slot, void *replacement, void **original) {
  int i = slot_index(slot);
  if (i < 0) return false;
  if (original) *original = vtable_[i];
  vtable_[i] = replacement;
  return true;
}

void *LauncherPlatform::global(engine::Global g) {
  switch (g) {
    case engine::Global::KeyboardInputs: return reinterpret_cast<void *>(a_.keyboard_inputs);
    case engine::Global::KeyboardStates: return reinterpret_cast<void *>(a_.keyboard_states);
  }
  return nullptr;
}

void LauncherPlatform::mouse_feed(int btn, int state, int x, int y, int dx, int dy) {
  apple::feed_mouse(a_.mouse_inputs, a_.mouse_inputs_grow, a_.mouse_device, a_.mouse_device_feed, btn, state, x, y, dx, dy);
}

}  // namespace launcher
}  // namespace mcfm
