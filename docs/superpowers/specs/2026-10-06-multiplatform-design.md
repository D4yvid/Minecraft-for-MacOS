# Multi-platform restructure: shared core + iOS + macOS + Android

## Goal
One public repo (`github.com/D4yvid/Minecraft-for-MacOS`, Apache-2.0) that builds mods for
Minecraft PE **0.15.10** on macOS (Catalyst-converted iOS app), iOS (injected IPA) and Android
(runet-client, injected APK), with game logic written once in a shared, platform-neutral core.
No Mojang files (IPA, APK, `.app`, `libminecraftpe.so`, assets) are ever committed; every build
takes the user's own copy as input.

## Constraints
- Shared core is **C++11**, no platform headers: Android must stay on NDK r10c + gnustl because
  `libminecraftpe.so` (armeabi-v7a) uses gnustl's `std::string`; Apple uses libc++. Shared code
  is compiled per platform with that platform's STL, so `std::string` returns match the engine.
- Apple binaries are stripped (engine located by IDA addresses + LC_UUID guard); Android keeps
  symbols (located by `dlsym` on `libminecraftpe.so`).
- macOS behaviour must not regress (all current tests + smoke keep passing).
- Android cannot be run here; it must at least compile (NDK r10c if obtainable).

## Layout
```
shared/
  include/mcfm/platform.h   Platform interface (below)
  include/mcfm/module.h     Module base + Client (module registry), C++11
  include/mcfm/engine.h     0.15.10 engine vocabulary: Slot / Global enums, KeyEvent, MouseAction
  include/mcfm/input/*.h    keymap, scroll accumulator, pointer policy, held set, delta accum (pure)
  src/                      client.cpp, input feeding (engine_input.cpp)
  modules/                  win10_ui.{h,cpp}, keyboard_mouse.{h,cpp}
  apple/                    AddressPlatform (Mach-O UUID guard, address table, vm_protect slot
                            patch), store receipt skip, address table for 0.15.10 arm64
  tests/                    host unit tests (all pure logic + module tests against a FakePlatform)
macos/   Catalyst glue: GameController/UIKit input, pointer lock, resize, title bar; app conversion
ios/     iOS glue + `make ios-ipa GAME=<ipa>` producing an injected, unsigned IPA
android/ runet-client (jni/, build files) using shared/ for Win10 UI; Substrate in third_party
tools/   inject.py, convert.sh (macOS), ida/ (RE scripts)
docs/    ARCHITECTURE.md (+ superpowers history), per-platform READMEs
```

## Interfaces (shared)
```cpp
namespace mcfm {
namespace engine {
enum class Slot { GetEdition, DefaultInputMode, UIScalingRules, UseCenteredGUI,
                  HideMousePointer, ShowMousePointer };          // AppPlatform virtuals
enum class Global { KeyboardInputs, KeyboardStates };            // std::vector<KeyEvent>, int[256]
struct KeyEvent { int32_t state; uint8_t key; uint8_t pad[3]; };  // 8 bytes on both ABIs
}
class Platform {
 public:
  virtual ~Platform() {}
  virtual void log(const char *msg) = 0;
  // Replace AppPlatform's virtual for `slot` in the live vtable. false = unknown on this platform.
  virtual bool patch_slot(engine::Slot slot, void *replacement) = 0;
  virtual void *global(engine::Global g) = 0;                     // null = unknown
  // Mouse::feed semantics: btn 0 move/1 left/2 right/3 middle/4 wheel; dx,dy relative look.
  virtual void mouse_feed(int btn, int state, int x, int y, int dx, int dy) = 0;
};
class Module { public: virtual ~Module() {} virtual const char *name() const = 0;
               virtual bool init(Platform &) = 0; };
class Client { public: explicit Client(Platform &); void add(Module *); bool init(); };
}
```
- `Win10UiModule(bool (*enabled)())`: patches GetEdition→"win10" / UIScalingRules→0 /
  UseCenteredGUI→true when `enabled()`, else "pocket"/platform default. Missing slots are skipped
  and logged.
- `KeyboardMouseModule(PointerCallbacks)`: patches DefaultInputMode→1, Hide/ShowMousePointer→
  callbacks; exposes `key(vk, down)`, `mouse_button/move_abs/move_rel/wheel` built on
  `global(KeyboardInputs/States)` (own push_back with `::operator new`, same allocator as the
  engine on both platforms) and `mouse_feed`.

## Platform implementations
- **apple/AddressPlatform**: today's `engine.mm` generalised: UUID guard, address table keyed by
  Slot/Global, vtable slot patch with vm_protect restore, `mouse_feed` = MouseDevice::feed
  (sub_1000201BC) for buttons/abs and a direct MouseAction push for relative motion.
- **android/RunetPlatform**: runet `hook::VirtualTable` on `_ZTV21AppPlatform_android23`, slot
  index found with `FindIndex(<base or android override symbol>)`; `global` via dlsym
  `_ZN8Keyboard7_inputsE` / `_ZN8Keyboard7_statesE`; `mouse_feed` via dlsym `_ZN5Mouse4feedEccssss`.
  Android keeps its own Options-screen toggle and config file, passing `enabled()` to Win10UiModule.

## Per-platform module sets
- macOS: Win10Ui(always) + KeyboardMouse + Catalyst glue + store skip (unchanged behaviour).
- iOS: Win10Ui(enabled on iPad or when `NSUserDefaults` `mcfm.win10ui` is true) + store skip.
  Keyboard/mouse on iPad is a later step (needs UIKit pointer lock instead of CoreGraphics).
- Android: Win10Ui(runet config toggle). Keyboard/mouse later.

## Build
Top-level `Makefile`: `make test` (host tests, no game files), `make macos` / `make app
GAME=<ipa|app>` / `make run` / `make check` (needs built app), `make ios` / `make ios-ipa
GAME=<ipa>`, `make android` (delegates to `android/Makefile`, needs NDK r10c + user APK).
`config.mk` (git-ignored) may set `GAME=`, `APK=`, `NDK=`. Inputs are validated: decrypted
(cryptid 0), arm64 slice UUID `01DFB489-A881-3BDD-8F98-6F016E409625`.

## Licensing
Apache-2.0 for the project (runet-client relicensed by its author). `android/third_party/substrate`
keeps LGPL-3.0 with its notice; AOSP-derived code keeps its header. NOTICE lists both.

## Testing
Host tests for all shared pure logic and both modules against a `FakePlatform` (records patched
slots, owns fake Keyboard vectors, records mouse_feed calls). macOS: existing bundle + smoke tests.
iOS: compile + `ios-ipa` structure test (dylib injected, LC_LOAD_DYLIB present). Android: compile
with NDK r10c when available; otherwise documented as untested.

## Changes during implementation (user decisions)
- No Module/Client registry: features are plain `install()` functions; a better module
  system comes later.
- Win10 UI is hardcoded on (as in the macOS mod) on every platform: no iPad/defaults
  toggle on iOS, no Options toggle on Android. It also patches PlatformType and
  UseMetadataDrivenScreens where known (Android).
- Android: runet's Client/modules, Substrate and minecraft headers are not imported; the
  library finds everything with dlsym and no longer links against libminecraftpe.so.
- Targets: Mac Catalyst and iOS 15.0 (current libc++ minimum).
