# Architecture

All three platforms run the same game build, Minecraft PE **0.15.10**, so the engine's
classes, virtual-function order and globals are the same everywhere. What differs is how
we *find* them and what the OS around the game looks like.

```
            ┌────────────────────── shared/ (C++11) ──────────────────────┐
            │ platform.h   Platform: log · patch_slot · global · mouse_feed │
            │ engine.h     Slot / Global enums, KeyEvent, mouse codes       │
            │ win10_ui     hardcoded desktop UI (edition, scaling, …)        │
            │ keyboard_mouse  input mode, pointer callbacks, key/mouse feed │
            │ input/       keymap, scroll, held keys (pure)                 │
            └───────▲───────────────────────▲─────────────────────▲─────────┘
                    │                       │                     │
   shared/apple: AddressPlatform     shared/apple (same)    android/: AndroidPlatform
   (IDA addresses + LC_UUID guard)                          (dlsym on libminecraftpe.so)
                    │                       │                     │
              macos/ glue                ios/ glue           android/ glue
   GameController + UIKit input,     Win10 UI only        JNI_OnLoad, runet hooking,
   pointer lock, resize, title bar                         APK packaging
```

## Platform interface (`shared/include/mcfm/platform.h`)

Shared code never touches addresses or symbol names. It asks the platform for four things:

| Call | macOS / iOS (`AddressPlatform`) | Android (`AndroidPlatform`) |
|---|---|---|
| `patch_slot(Slot, fn, &orig)` | write `AppPlatform_iOS` vtable at a known byte offset (vm_protect) | `VirtualTable("_ZTV21AppPlatform_android23").FindIndex(<symbol>)` |
| `global(Global)` | unslid address + ASLR slide | `dlsym("_ZN8Keyboard7_inputsE")`, … |
| `mouse_feed(btn, state, x, y, dx, dy)` | `MouseDevice::feed` (sub_1000201BC) / direct queue push for dx,dy | `dlsym("_ZN5Mouse4feedEccssss")` |
| `log(msg)` | `NSLog("mcfm: …")` | `__android_log_print("mcfm", …)` |

A slot a platform doesn't know returns `false`; features log it and carry on.

### Why C++11 and per-platform compilation

Android must use NDK r10c + gnustl because `libminecraftpe.so` was built against gnustl's
`std::string`. Shared code is compiled by each platform with *its* STL, so a replacement
like `std::string get_edition(void*)` returns exactly the layout the engine expects.
Host tests compile shared code with `-std=c++11` to keep it that way.

### Apple: finding things in a stripped binary

`shared/apple/addresses_0_15_10.h` holds IDA addresses. They are used only after
`AddressPlatform::attach()` confirms the image's LC_UUID and that the vtable slots hold the
expected functions; otherwise the mod disables itself. `tools/check_game.sh` applies the
same check to inputs at build time.

## Features

- **win10_ui** (`shared/src/win10_ui.cpp`): GetEdition→"win10", UIScalingRules→0,
  UseCenteredGUI→true, PlatformType→0, UseMetadataDrivenScreens→true. Hardcoded on.
- **keyboard_mouse** (`shared/src/keyboard_mouse.cpp`): DefaultInputMode→1 (mouse),
  Hide/ShowMousePointer→callbacks, `key()` pushes into `Keyboard::_inputs`
  (own push_back with the global allocator), mouse calls go through `mouse_feed`.
  Used on macOS; iOS/Android keep touch.

## Adding a feature

1. Add the engine pieces you need to `engine.h` (`Slot` / `Global`).
2. Map them in each platform (`address_platform.mm`: offsets from IDA; `android_platform.cpp`:
   symbol names — `nm -D libminecraftpe.so` from your APK helps find iOS counterparts).
3. Write the feature in `shared/src/` against `Platform`, with a test in
   `shared/tests/features_test.cpp` using `FakePlatform`.
4. Call its `install()` from the platform entry points (`macos/src/main.mm`,
   `ios/src/main.mm`, `android/jni/main.cpp`).

## Finding addresses (iOS)

`tools/ida/` has the idalib scripts used to locate everything (see its README). The
Android library keeps symbols, which makes it the easiest place to identify a function
before looking for it in the stripped iOS binary.

## Loading the mod

- macOS: `macos/tools/convert.sh` copies your app, retags binaries for Mac Catalyst,
  removes `UIRequiresFullScreen`, renames it to `minecraftpe`, injects
  `LC_LOAD_DYLIB @executable_path/Frameworks/libmcfm.dylib` (`tools/inject.py`) and
  signs it ad hoc.
- iOS: `ios/tools/make_ipa.sh` does the injection into an arm64-only IPA, unsigned.
- Android: `android/tools/build_apk.sh` adds `System.loadLibrary("runet")` after
  gnustl_shared in `MainActivity` (`patch_smali.py`), adds the libraries, rebuilds with
  apktool and debug-signs.
