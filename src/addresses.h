#pragma once
#include <cstdint>

// Unslid addresses in minecraftpe2 0.15.10 (iOS arm64), image base 0x100000000.
// Found with IDA; see docs/superpowers/specs/2026-10-06-mcpe-kbm-design.md.
namespace addr {

// AppPlatform
constexpr uintptr_t kIOSPlatformVtable = 0x100EABE00;  // vptr value of AppPlatform_iOS
constexpr uintptr_t kSlotHideMousePointer = 104;
constexpr uintptr_t kSlotShowMousePointer = 112;
constexpr uintptr_t kSlotGetEdition = 744;
constexpr uintptr_t kSlotDefaultInputMode = 768;
constexpr uintptr_t kSlotUIScalingRules = 792;

// What those slots must hold in the expected binary (guard).
constexpr uintptr_t kFnHideMousePointer = 0x100460E38;  // base AppPlatform, empty
constexpr uintptr_t kFnShowMousePointer = 0x100460E3C;  // base AppPlatform, empty
constexpr uintptr_t kFnGetEdition = 0x100460ABC;        // returns "pocket"
constexpr uintptr_t kFnDefaultInputModeIOS = 0x1007074F4;  // returns 2 (touch)
constexpr uintptr_t kFnUIScalingRulesIOS = 0x1007074EC;    // returns 1 (pocket)

// Keyboard
constexpr uintptr_t kKeyboardInputs = 0x100F59FF8;      // std::vector<KeyEvent>
constexpr uintptr_t kKeyboardStates = 0x100F59BF8;      // int32_t[256]
constexpr uintptr_t kKeyboardInputsGrow = 0x10070B400;  // push_back slow path (vec*, elem*)

// Mouse
constexpr uintptr_t kMouseDevice = 0x100F5A040;        // MouseDevice Mouse::_instance
constexpr uintptr_t kMouseInputs = 0x100F5A058;        // its std::vector<MouseAction>
constexpr uintptr_t kMouseInputsGrow = 0x100020404;    // push_back slow path (vec*, elem*)
constexpr uintptr_t kMouseDeviceFeed = 0x1000201BC;    // (dev, btn, state, x, y)

// App (C++ object held in minecraftpeViewController->_app), vtable indices
constexpr int kAppSlotSetSize = 21;          // (app, int w, int h)
constexpr int kAppSlotSetSizeAndScale = 20;  // (app, int w, int h, float 0)

}  // namespace addr
