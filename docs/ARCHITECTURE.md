# Architecture

All three platforms run the same game build, Minecraft PE **0.15.10**, so the engine's
classes, virtual-function order and globals are the same everywhere. What differs is how
we *find* them and what the OS around the game looks like.

There are two generations of builds:

- **The launchers** (today's products): the **iOS** arm64 binary runs on every platform. Our
  launcher converts and loads it, never runs its iOS glue, implements `AppPlatform` itself and
  drives the engine. macOS (`make app`), our Android app (`make android-app`), our iOS app
  (`make ios-app`). Plan, stages and decisions: [LAUNCHER.md](LAUNCHER.md).
- **The mods** (deprecated / legacy): a library injected into the platform's own game (Mac
  Catalyst app, the iOS IPA, Mojang's Android APK) that patches its `AppPlatform` vtable. The
  sections from "Platform interface" on describe them; the launchers reuse their shared input
  code through `LauncherPlatform`.

## Launchers

```
 your decrypted IPA ──► converter: MH_EXECUTE → MH_DYLIB, frameworks → stubs, hooks patched in
                        (tools/launcher/mcfm_image.py; shared/loader/convert.cpp on Android)
                                       │
       ┌───────────────────────────────┼───────────────────────────────┐
     macOS (macos/launcher)       Android (android/launcher, app/)   iOS (ios/launcher, app/)
     our loader (or dyld)         our loader                         dyld (signed at build)
     host libSystem + libc++      Darwin layer + Apple-ABI libc++    system libSystem + libc++
     ANGLE (Metal)                system GLES 3 (EGL)                ANGLE (Metal, static build)
     AudioToolbox → CoreAudio     AudioToolbox → AAudio              system AudioToolbox
       └───────────────────────────────┼───────────────────────────────┘
                     shared/launcher (C++11): our AppPlatform (app_platform), engine boot and
                     frame (engine), hook table seams (seams), text and touch input,
                     LauncherPlatform → shared/src keyboard_mouse
```

- `shared/loader/` — our Mach-O loader (parse, map, fixups, imports through `LoaderOS`,
  initializers, unwind registration) and the converter in C++. Platform layers:
  `macos/launcher/loader_macos.cpp`, `android/launcher/loader_android.cpp`.
- `shared/launcher/` — `app_platform` (the base `AppPlatform` object with our vtable: paths,
  Win10 edition, input mode, keyboard, image picker), `engine` (the boot sequence of
  `minecraftpeViewController` and the frame), `seams` (engine functions replaced through the hook
  table: telemetry HTTP, store, Xbox Live), `text_input`, `touch_input`, `launcher_platform`
  (`mcfm::Platform` for the launcher), `game_import` (Android's on-device import).
- `shared/apple/hook_table` and `macho_uuid` — the hook table filled before initializers and the
  LC_UUID guard, used by every launcher.
- Engine code is patched only at conversion; at runtime only the hook table is written.

## Mods (deprecated / legacy)

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
   GameController + UIKit input,     Win10 UI only        JNI_OnLoad, vtable hooking,
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
Host tests compile shared code with `-std=c++11` to keep it that way. (The legacy Android mod
is the gnustl case. The launchers run the iOS image everywhere, so their `std::string` is Apple's
libc++: the host's on macOS/iOS, our Apple-ABI build in `android/launcher/runtime/` on Android.)

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
  Used by the Catalyst mod (the legacy iOS/Android mods keep touch) and by all three launchers
  through `LauncherPlatform` (the Android and iOS apps without pointer capture, in touch mode).
- The launchers do not use `win10_ui`: their own AppPlatform (`shared/launcher/app_platform.cpp`)
  returns the Win10 edition and desktop policy directly.

## Adding a feature

1. Add the engine pieces you need to `engine.h` (`Slot` / `Global`).
2. Map them in each platform (`address_platform.mm`: offsets from IDA; `android_platform.cpp`:
   symbol names — `nm -D libminecraftpe.so` from your APK helps find iOS counterparts).
3. Write the feature in `shared/src/` against `Platform`, with a test in
   `shared/tests/features_test.cpp` using `FakePlatform`.
4. Call its `install()` from the platform entry points: the launchers
   (`macos/launcher/input.mm`, `ios/launcher/launcher.mm`, `android/launcher/game_thread.cpp`,
   with a `LauncherPlatform`) and, if the deprecated/legacy mods need it, `macos/src/main.mm`,
   `ios/src/main.mm`, `android/jni/main.cpp`. A slot the launchers' own AppPlatform should answer
   goes in `shared/launcher/app_platform.cpp` with a test in `launcher_app_platform_test.cpp`.

## Finding addresses (iOS)

Named slot maps: [research/appplatform-vtable.md](research/appplatform-vtable.md),
[research/minecraftclient-vtable.md](research/minecraftclient-vtable.md); renderer notes:
[research/renderer.md](research/renderer.md).


`tools/ida/` has the idalib scripts used to locate everything (see its README). The
Android library keeps symbols, which makes it the easiest place to identify a function
before looking for it in the stripped iOS binary.

## Loading the mod (deprecated / legacy)

The launchers load the game themselves (see "Launchers" above and [LAUNCHER.md](LAUNCHER.md)).

- macOS (Catalyst, deprecated): `macos/tools/convert.sh` copies your app, retags binaries for Mac Catalyst,
  removes `UIRequiresFullScreen`, renames it to `minecraftpe`, injects
  `LC_LOAD_DYLIB @executable_path/Frameworks/libmcfm.dylib` (`tools/inject.py`) and
  signs it ad hoc.
- iOS (legacy): `ios/tools/make_ipa.sh` does the injection into an arm64-only IPA, unsigned.
- Android (legacy): `android/tools/build_apk.sh` adds `System.loadLibrary("mcfm")` after
  gnustl_shared in `MainActivity` (`patch_smali.py`), adds the libraries, rebuilds with
  apktool and debug-signs.
