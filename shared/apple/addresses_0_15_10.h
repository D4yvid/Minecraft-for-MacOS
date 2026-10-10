#pragma once
#include <cstdint>

// Unslid addresses in minecraftpe2 0.15.10 (iOS arm64), image base 0x100000000.
// Found with IDA (tools/ida); see docs/ARCHITECTURE.md. Only used after the LC_UUID guard.
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
constexpr uintptr_t kKeyboardText = 0x100F5A010;        // std::vector<{std::string; bool}> typed text

// Mouse
constexpr uintptr_t kMouseDevice = 0x100F5A040;        // MouseDevice Mouse::_instance
constexpr uintptr_t kMouseInputs = 0x100F5A058;        // its std::vector<MouseAction>
constexpr uintptr_t kMouseInputsGrow = 0x100020404;    // push_back slow path (vec*, elem*)
constexpr uintptr_t kMouseDeviceFeed = 0x1000201BC;    // (dev, btn, state, x, y)

// App (C++ object held in minecraftpeViewController->_app), vtable indices
constexpr int kAppSlotSetSize = 21;          // (app, int w, int h)
constexpr int kAppSlotSetSizeAndScale = 20;  // (app, int w, int h, float 0)

// Mach-O launcher boot (docs/research/macho-launcher.md, "Boot sequence")
constexpr uintptr_t kFnAppPlatformCtor = 0x10045F678;      // AppPlatform::AppPlatform(), sets the singleton
constexpr uintptr_t kBaseAppPlatformVtable = 0x100E649C0;  // vptr value of the base AppPlatform
constexpr uintptr_t kAppPlatformSingleton = 0x100F5E850;
constexpr uintptr_t kAppPlatformSize = 0x210;              // AppPlatform_iOS; the base object is 360 bytes
constexpr uintptr_t kFnMinecraftClientCtor = 0x10006E2DC;  // (this, int argc, char **argv)
constexpr uintptr_t kMinecraftClientSize = 0x428;
constexpr uintptr_t kFnAppInit = 0x1000555BC;              // App::init(AppContext &)
constexpr uintptr_t kFnGraphicsVendor = 0x10003A850;       // std::string from glGetString(GL_VENDOR)
constexpr uintptr_t kFnGraphicsRenderer = 0x10003A8AC;
constexpr uintptr_t kFnGraphicsVersion = 0x10003A680;
constexpr uintptr_t kFnGraphicsExtensions = 0x10003A908;
constexpr int kAppSlotUpdate = 19;                         // App::update()
// Seams (hooked by the launcher)
constexpr uintptr_t kFnXblAppConfig = 0x100798B34;         // Xbox services config singleton (seam #3)
constexpr uintptr_t kFnCreateStores = 0x100711774;         // StoreFactory::createStores, iOS (seam #2)
constexpr uintptr_t kFnTelemetryUpload = 0x1003B42A8;      // posts a telemetry event batch via the iOS HTTP glue (seam #1)

}  // namespace addr
