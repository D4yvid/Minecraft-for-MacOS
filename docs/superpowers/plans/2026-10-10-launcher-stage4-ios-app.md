# Stage 4 — Our Own iOS App (Swift) Implementation Plan

> **Note (as built, 2026-10-10):** the implementation departs from this plan in two places. The
> system OpenGL ES drew nothing (the engine binds framebuffer 0, which iOS does not have), so
> graphics go through **ANGLE** (OpenGL ES 3 on Metal, an EGL window surface on a `CAMetalLayer`;
> the owner's call), not EAGL / `CAEAGLLayer`. The Swift app builds with `swiftc -swift-version 5`,
> not Swift 6. See docs/research/ios-app.md and docs/LAUNCHER.md (Stage 4) for what was built.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Swift iOS app of our own (like the Android app) that runs Minecraft PE 0.15.10's iOS game image, bundled in the app, with our launcher's AppPlatform — no patched Mojang IPA, no game UI glue.

**Architecture:** iOS forbids unsigned executable memory, so the game binary is converted at build time into a dylib (`mcfm_image.py dylib --hooks`, tagged for iOS), embedded in the app's `Frameworks/` with stubs for the Apple frameworks the game's iOS glue imports, signed with the owner's certificate and loaded by **dyld** (the macOS launcher's `--loader dyld` path, Stage 1). OpenGLES, AudioToolbox, libSystem, libc++ and libz stay the real iOS ones (everything the game imports from them exists in the iOS 27 SDK). The engine runs on the main thread from a `CADisplayLink` (as the Mac launcher's timer) in an `EAGLContext` (OpenGL ES 3) drawing into a `CAEAGLLayer` framebuffer; a C API (`ios/launcher/mcfm_ios.h`, Objective-C++) connects the Swift app to `shared/launcher`.

**Tech Stack:** Swift 6 (swiftc, no Xcode project), Objective-C++ (C++17, `shared/` stays C++11), OpenGL ES 3 / EAGL, ibtool, codesign, devicectl; make.

**Spec:** docs/LAUNCHER.md (Stage 3's Android app is the model; this plan adds Stage 4). Owner decisions (2026-10-10): a new Swift app instead of editing the IPA; native OpenGL ES; make + command-line build; the game bundled (as the Android APK).

## Global Constraints

- Device: the owner's iPhone 16 Pro Max (iPhone17,2), iOS 27, connected over USB; Xcode 27 at `/Applications/Xcode.app`, used through `DEVELOPER_DIR` (no `sudo xcode-select`).
- Signing: Apple Development identity of team `JZ6566VD35` (Personal Team), bundle id `io.github.d4yvid.mcfm.ios`, profile created by Xcode (free: 7 days); reuse `ios/tools/sign_install.sh`'s lookup.
- Deployment target iOS 15.0; arm64 only.
- The game is bundled (binary converted, `data/`): the app is a local build in `dist/` (git-ignored), never committed (`tools/tests/no_game_files_test.sh`).
- `shared/` stays C++11 and platform-free; logs prefixed `mcfm:`; `make test` green before every commit.
- Apple addresses only behind the LC_UUID guard (`is_expected_game_image` before `dlopen`).
- Engine code is patched only at conversion (hooks); never write to `__TEXT` at runtime (impossible on iOS anyway).
- The IPA mod (`make ios`, `ios-ipa`, `ios-device`) stays as legacy until this app plays.

## Review Focus

1. **Background/foreground:** the game must save when the app resigns active (home, app switcher, lock) and draw again on return, without drawing while in the background (iOS kills apps that use GL in the background). Task 3 implements it; Task 5's checklist exercises it on the device.
2. **Screen geometry:** full screen at native resolution (`nativeScale`, 2868×1320 on this phone), no letterboxing, rotation between the two landscape orientations; the framebuffer must be rebuilt on size changes. Task 3 test: `layout_test` for the pixel size math; Task 5 screenshot check.
3. **First frame target:** iOS has no framebuffer 0; the app binds its FBO before every `frame()` (as the game's own `-[EAGLView setFramebuffer]`). Task 5's on-device screenshot check proves it.
4. **Signing failures:** an expired free profile or no device must give a clear message, not a raw `devicectl` error (Task 4 tests the lookup with a fixture profile).
5. **Text input:** the soft keyboard's text, backspace and return reach the engine's text box (return also presses Enter, as on Android/iOS), and the keyboard hides when the game asks. Task 3 + Task 5 checklist.

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/launcher/mcfm_image.py` (modify) | `dylib --platform ios`: keep an iOS platform tag; `--host OpenGLES,AudioToolbox` keeps those imports real |
| `tools/launcher/build_stubs.sh` (modify) | `--target ios`: stubs and stub runtime as iOS dylibs |
| `shared/launcher/touch_input.{h,cpp}` (moved from `android/launcher/input_events.*`'s `TouchSlots`/`touch_feed`) | touch slots and `Multitouch::feed` calls, shared by Android and iOS |
| `ios/launcher/mcfm_ios.h` | the C API the Swift app calls |
| `ios/launcher/launcher.mm` | load the image with dyld + hooks, start the engine, frame, lifecycle, input, keyboard/picker callbacks |
| `ios/launcher/ios_keymap.{h,cpp}` + `ios/tests/ios_keymap_test.cpp` | UIKeyboardHIDUsage → Windows VK (hardware keyboards) |
| `ios/launcher/layout.h` + `ios/tests/layout_test.cpp` | pixel size from points × scale, landscape normalisation |
| `ios/app/*.swift` | `AppDelegate`, `GameViewController` (EAGL view, display link, touches, `UIKeyInput`, PHPicker), `Info.plist`, `LaunchScreen.storyboard` |
| `ios/tools/build_app.sh` | assemble `dist/ios/mcfm.app`: convert, stubs, compile, bundle `data/`, sign |
| `ios/tools/signing.sh` (from `sign_install.sh`) | profile/identity/device lookup, shared by `ios-device` and `ios-app` |
| `ios/tools/app_check.sh` | on-device check: launch with `--frames`, copy the screenshot out of the container, `check_screenshot.py` |
| `Makefile` | `ios-app`, `ios-app-run`, `ios-app-check`; host tests in `make test` |

---

### Task 1: Converter and stubs for iOS

**Files:**
- Modify: `tools/launcher/mcfm_image.py` (`cmd_dylib`: `platform` and `host_libs` parameters; CLI `--platform ios`, `--host LIB[,LIB]`)
- Modify: `tools/launcher/build_stubs.sh` (`--target ios`)
- Test: `tools/tests/launcher_converter_edges_test.sh` (add iOS cases), `tools/tests/launcher_stubs_test.sh` (add `--target ios`)

**Interfaces:**
- Produces: `mcfm_image.py dylib <exe> <out> --hooks <tsv> --platform ios --host OpenGLES,AudioToolbox` → MH_DYLIB with `LC_BUILD_VERSION` platform 2 (iOS), minos 15.0, sdk 15.0; `OpenGLES`/`AudioToolbox` loads kept as their `/System/Library/Frameworks/...` paths; other frameworks → `@rpath/mcfm_stub_<lib>.dylib`. Default (no flags) output byte-identical to today (convert_test.sh keeps comparing the C++ converter with it).
- Produces: `build_stubs.sh --target ios <imports.tsv> <outdir>` → `libmcfm_stubrt.dylib`, `mcfm_stub_<lib>.dylib` built with `xcrun --sdk iphoneos clang -target arm64-apple-ios15.0`, install names `@rpath/...`; libraries passed with `--skip OpenGLES --skip AudioToolbox` get no stub.

- [ ] **Step 1: Write the failing tests** — in `launcher_converter_edges_test.sh`, convert the loader fixture with `--platform ios --host CoreFoundation` and assert with `otool -l`: `platform 2`, `minos 15.0`; `otool -L` shows `/System/Library/Frameworks/CoreFoundation.framework/...` kept and no `mcfm_stub_CoreFoundation`; and the default conversion's bytes are unchanged (`cmp` with a run without flags saved before). In `launcher_stubs_test.sh`, when `xcrun --sdk iphoneos --show-sdk-path` succeeds: `build_stubs.sh --target ios` on the fixture TSV, `vtool -show-build` says `platform IOS` for every dylib, and `--skip` drops that library's stub; when the SDK is missing, print `skipped (no iPhoneOS SDK)`.
- [ ] **Step 2: Run them** — `bash tools/tests/launcher_converter_edges_test.sh; bash tools/tests/launcher_stubs_test.sh` → FAIL (`unknown option --platform`, `unknown target ios`).
- [ ] **Step 3: Implement** — `cmd_dylib(src, dst, hooks=(), platform="macos", host_libs=())`: the tag becomes `struct.pack("<IIIIII", LC_BUILD_VERSION, 24, PLATFORM_IOS=2, IOS_15=0x000F0000, IOS_15, 0)` for iOS; a load whose `short_name` is in `HOST_LIBS | set(host_libs)` is kept as is. `build_stubs.sh`: `ios` sets `CC=(xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -O1 -Wall -dynamiclib)` and accepts `--skip <lib>` (repeatable; the `.c` file for that lib is not compiled).
- [ ] **Step 4: Run** the two tests and `make test` → PASS; `bash tools/tests/convert_test.sh` still passes (default bytes unchanged).
- [ ] **Step 5: Commit** — `iOS: converter and stubs for an iOS-tagged game image`.

### Task 2: Touch input shared between Android and iOS

**Files:**
- Create: `shared/launcher/touch_input.h`, `shared/launcher/touch_input.cpp` (moved `TouchSlots`, `TouchAction`, `FeedCall`, `touch_feed` from `android/launcher/input_events.*`)
- Modify: `android/launcher/input_events.{h,cpp}` (include `touch_input.h`), `android/launcher/tests/input_events_test.cpp`, `Makefile` (`LAUNCHER_SO_SRCS`, test rules)
- Test: `shared/tests/touch_input_test.cpp` (the touch cases moved from `input_events_test.cpp`)

**Interfaces:**
- Produces (namespace `mcfm::launcher`): `enum class TouchAction { Down, Move, Up, Cancel }`; `class TouchSlots { int down(int pointer); int find(int pointer) const; int up(int pointer); }`; `struct FeedCall { int button, state, x, y, slot; }`; `bool touch_feed(TouchSlots *, TouchAction, int pointer, float x, float y, FeedCall *out)`. `android/launcher` keeps `using` aliases so its code is unchanged.

- [ ] **Step 1: Move the tests** — create `shared/tests/touch_input_test.cpp` with the existing touch cases (12 slots, reuse after up, cancel, unknown pointer) against `mcfm::launcher`; add it to `SHARED_TESTS`.
- [ ] **Step 2: Run** `make test` → FAIL (`touch_input.h` not found).
- [ ] **Step 3: Move the code** (no behaviour change) and alias it in `android/launcher/input_events.h`.
- [ ] **Step 4: Run** `make test`, `make android-test` and `make android-app-check` (an emulator running) → PASS.
- [ ] **Step 5: Commit** — `Touch input shared by the Android and iOS apps`.

### Task 3: The iOS launcher core and the Swift app

**Files:**
- Create: `ios/launcher/mcfm_ios.h`, `ios/launcher/launcher.mm`, `ios/launcher/ios_keymap.{h,cpp}`, `ios/launcher/layout.h`
- Create: `ios/app/AppDelegate.swift`, `ios/app/GameViewController.swift`, `ios/app/GameView.swift`, `ios/app/Info.plist`, `ios/app/LaunchScreen.storyboard`, `ios/app/Bridging.h`
- Test: `ios/tests/ios_keymap_test.cpp`, `ios/tests/layout_test.cpp` (host, in `make test`)

**Interfaces:**
- Consumes: Task 1's image and stubs (built by Task 4), Task 2's `touch_feed`, `shared/launcher` (`Engine`, `LauncherPlatform`, `make_host_info`, `set_keyboard_callbacks`, `set_image_picker`, `image_picked`, `image_pick_cancelled`, `push_text`/`push_backspace`/`push_return`, `hooks()`), `shared/apple/hook_table.h`, `macho_uuid.h`, `keyboard_mouse`.
- Produces (C, `mcfm_ios.h`):
```c
typedef struct {
  void (*show_keyboard)(void);
  void (*hide_keyboard)(void);
  void (*pick_image)(void);
  void (*fatal)(const char *message);
} McfmIosCallbacks;
int  mcfm_ios_start(const char *image_path, const char *data_dir, const char *home_dir,
                    int width_px, int height_px, McfmIosCallbacks callbacks);  // 1 ok
void mcfm_ios_frame(void);                 // one engine frame (the FBO is bound by the caller)
void mcfm_ios_resize(int width_px, int height_px);
void mcfm_ios_pause(int paused);           // 1: suspend (the game saves before it returns)
void mcfm_ios_focus(int focused);
void mcfm_ios_touch(int action, int pointer, float x_px, float y_px);
void mcfm_ios_key(int hid_usage, int down);
void mcfm_ios_text(const char *utf8);
void mcfm_ios_backspace(void);
void mcfm_ios_return(int press_enter);
void mcfm_ios_image_picked(const char *png_path_or_null);
```
- Produces (C++): `int mcfm::launcher::ios_hid_to_vk(int usage)` (0 = no key); `layout.h`: `struct PixelSize { int w, h; }; PixelSize pixel_size(double w_pt, double h_pt, double scale)` (rounded, landscape: w ≥ h).

- [ ] **Step 1: Write the failing host tests** — `ios_keymap_test.cpp`: letters `0x04..0x1D` → `'A'..'Z'`, digits `0x1E..0x26` → `'1'..'9'`, `0x27` → `'0'`, Return `0x28` → `0x0D`, Escape `0x29` → `0x1B`, Backspace `0x2A` → `0x08`, Tab → `0x09`, Space → `0x20`, arrows `0x4F..0x52` → `0x27,0x25,0x28,0x26`, left/right shift/ctrl/alt → `0x10/0x11/0x12`, F1..F12 → `0x70..0x7B`, unknown `0x00` → 0. `layout_test.cpp`: `pixel_size(956, 440, 3)` = `{2868, 1320}`; portrait input `(440, 956, 3)` → `{2868, 1320}`; `(1024.5, 768, 2)` rounds to `{2049, 1536}`. Add both to a new `IOS_TESTS` list in `make test` (host clang++, `-Iios/launcher`).
- [ ] **Step 2: Run** `make test` → FAIL (headers missing).
- [ ] **Step 3: Implement** `ios_keymap.cpp` (a `switch`/ranges) and `layout.h`. Run → PASS.
- [ ] **Step 4: `launcher.mm`** — following `macos/launcher/main.mm`'s dyld path: check `is_expected_game_image` on the file, register `_dyld_register_func_for_add_image` to find the image's header and slide and fill the hook table (`hook_table.h`), `dlopen(image_path, RTLD_NOW | RTLD_LOCAL)`; `make_host_info(home, data, home + "/tmp")` with `input_mode = 2`; `engine.start(EngineAddresses::for_slide(slide), info, w, h)`; `LauncherPlatform` + `keyboard_mouse::install(..., false)`; `set_keyboard_callbacks` / `set_image_picker` forward to the Swift callbacks; `mcfm_ios_frame` = `engine.frame()` + drain of the main queue is not needed (real libdispatch); pause/resume as Android's `sync_pause` (suspend saves, resume then focus); touches through `touch_feed` and `Multitouch::feed` (`addr::kMultitouchFeed + slide`); keys through `ios_hid_to_vk` and `keyboard_mouse::key`; text through `text_input.h`; `mcfm_ios_return(1)` also presses and releases VK 13 (iOS's own `textViewShouldReturn`). Errors → `callbacks.fatal(message)`; log `mcfm: engine started (WxH)` and `mcfm: paused (saved)` as Android does.
- [ ] **Step 5: The Swift app** —
  - `AppDelegate`: one window, `GameViewController` as root; `applicationWillResignActive` → `mcfm_ios_pause(1)` (synchronous save), `applicationDidBecomeActive` → `mcfm_ios_pause(0)`; activates `AVAudioSession` category `.ambient` before the engine starts (FMOD's RemoteIO needs an active session).
  - `GameView` (`UIView`, `layerClass = CAEAGLLayer`, `contentScaleFactor = window.screen.nativeScale`, opaque): `EAGLContext(api: .openGLES3)`, one framebuffer with a color renderbuffer from `renderbufferStorage(_:from:)` and a depth24/stencil8 renderbuffer; rebuilt in `layoutSubviews` when the pixel size (from `layout.h` through a tiny C shim `mcfm_ios_pixel_size`) changes, then `mcfm_ios_resize`.
  - `GameViewController`: `prefersStatusBarHidden = true`, `prefersHomeIndicatorAutoHidden = true`, `preferredScreenEdgesDeferringSystemGestures = .all`, `supportedInterfaceOrientations = .landscape`; a `CADisplayLink` (preferred 60 fps) calls, when active and started: `EAGLContext.setCurrent`, bind the FBO, `mcfm_ios_frame()`, `presentRenderbuffer`. First layout → `mcfm_ios_start` with paths in the bundle (`Bundle.main.privateFrameworksPath + "/libminecraftpe.dylib"`, `Bundle.main.resourcePath + "/game/data/"`) and home = Application Support `/home` (created). Touches: `touchesBegan/Moved/Ended/Cancelled` with a stable small id per `UITouch` (dictionary `ObjectIdentifier → Int`, freed on end), coordinates × `contentScaleFactor`. Hardware keys: `pressesBegan/Ended` → `mcfm_ios_key(press.key.keyCode.rawValue, …)`. Soft keyboard: the view adopts `UIKeyInput` (`insertText` → `mcfm_ios_text` or `"\n"` → `mcfm_ios_return(1)`, `deleteBackward` → `mcfm_ios_backspace`), `becomeFirstResponder` / `resignFirstResponder` on the callbacks. Skin picker: `PHPickerViewController` (images, 1), the result re-encoded as PNG at `home/tmp/newSkin.png` → `mcfm_ios_image_picked(path)`, cancel → `nil`. `fatal` → `UIAlertController` with "Close" (`exit(0)` after).
  - `Info.plist`: `CFBundleIdentifier` `$(BUNDLE_ID)`, `CFBundleExecutable` `mcfm`, display name "Minecraft PE (mcfm)", `MinimumOSVersion` 15.0, `UIRequiresFullScreen`, `UIStatusBarHidden`, `UISupportedInterfaceOrientations` both landscapes, `UILaunchStoryboardName` `LaunchScreen`, `LSApplicationCategoryType` `public.app-category.games`, `GCSupportsGameMode`, `UIFileSharingEnabled`, `NSPhotoLibraryUsageDescription` not needed (PHPicker), `UIDeviceFamily` [1, 2].
- [ ] **Step 6: Run** `make test` → PASS (host parts); the app is built and run in Task 4/5.
- [ ] **Step 7: Commit** — `iOS: the launcher core and the Swift app`.

### Task 4: Building, signing and installing the app

**Files:**
- Create: `ios/tools/build_app.sh`, `ios/tools/signing.sh`
- Modify: `ios/tools/sign_install.sh` (uses `signing.sh`), `Makefile` (`ios-app`, `ios-app-run`)
- Test: `ios/tests/signing_test.sh` (in `make test`; no device needed)

**Interfaces:**
- Consumes: Task 1's converter/stubs, Task 3's sources.
- Produces: `ios/tools/signing.sh` functions `mcfm_find_device` (UDID of the first paired iOS device, or empty), `mcfm_find_profile <bundle id> <udid>` (path of the newest unexpired profile for that id containing the device), `mcfm_identity <team>`; `make ios-app IPA=…` → `dist/ios/mcfm.app` (signed) and `dist/ios/mcfm.ipa`; `make ios-app-run` installs and launches with `devicectl device process launch --console` (logs in the terminal).

- [ ] **Step 1: Write the failing test** — `signing_test.sh` builds fixture profiles as plists (`security cms` cannot sign them, so `signing.sh` reads a profile through a `MCFM_DECODE_PROFILE` command, `security cms -D -i` by default, `cat` in the test): one expired, one for another bundle id, one without the device, one valid → `mcfm_find_profile` returns the valid one; none valid → empty and the caller's message names the bundle id and says to choose the team in Xcode.
- [ ] **Step 2: Run** `bash ios/tests/signing_test.sh` → FAIL (`signing.sh` missing).
- [ ] **Step 3: Implement `signing.sh`** (lifted from `sign_install.sh`, which now sources it) → test PASS.
- [ ] **Step 4: `build_app.sh`** — inputs: IPA path, out dir, bundle id, `DEVELOPER_DIR`. Steps: unzip `Payload/*.app/minecraftpe2` and `data/`; `tools/launcher/thin_arm64.sh`; `mcfm_image.py imports` → `imports.tsv`; `mcfm_image.py dylib --hooks <hooks.tsv> --platform ios --host OpenGLES,AudioToolbox` → `mcfm.app/Frameworks/libminecraftpe.dylib` (hooks from the launcher's `hooks()`: a tiny host tool `ios/tools/print_hooks.cpp` built with `shared/launcher/seams.cpp`, as `mcfm-launch --print-hooks` does); `build_stubs.sh --target ios … --skip OpenGLES --skip AudioToolbox` → `Frameworks/`; compile `shared/` + `ios/launcher/*.mm|cpp` with `xcrun --sdk iphoneos clang++ -target arm64-apple-ios15.0 -std=c++17 -fobjc-arc` to objects; `swiftc -target arm64-apple-ios15.0 -sdk … -import-objc-header ios/app/Bridging.h ios/app/*.swift <objects> -lc++ -framework OpenGLES -framework QuartzCore -framework UIKit -framework AVFoundation -framework PhotosUI -Xlinker -rpath -Xlinker @executable_path/Frameworks -o mcfm.app/mcfm`; `Info.plist` with the bundle id substituted; `ibtool --compile mcfm.app/LaunchScreen.storyboardc ios/app/LaunchScreen.storyboard`; `data/` → `mcfm.app/game/data/`; app icon: the IPA's `AppIcon60x60@3x.png` as `CFBundleIcons`; sign: every dylib in `Frameworks/`, then the app with the profile's entitlements (`signing.sh`); `codesign --verify --deep --strict`; zip `Payload/mcfm.app` → `mcfm.ipa`.
- [ ] **Step 5: Makefile** — `ios-app: … $(IPA)` (needs `IPA`, as `android-app`; error message if missing); `ios-app-run: ios-app` → `devicectl device install app` + `process launch --console --terminate-existing io.github.d4yvid.mcfm.ios`.
- [ ] **Step 6: Run** `make ios-app` → `dist/ios/mcfm.app` signed (`codesign -dv` shows team JZ6566VD35); `make ios-app-run` on the phone → `mcfm: engine started (2868x1320)` in the console. Fix what the device shows (missing stub symbols appear as `mcfm: stub …` lines, dyld errors in the console).
- [ ] **Step 7: Commit** — `iOS: build, sign and install the app (make ios-app, ios-app-run)`.

### Task 5: On-device check and the hand checklist

**Files:**
- Create: `ios/tools/app_check.sh`; Modify: `ios/launcher/launcher.mm` (`--frames N --screenshot <path>` launch arguments, as `mcfm-launch`), `Makefile` (`ios-app-check`)

**Interfaces:**
- Consumes: Task 4's app; `tools/android/check_screenshot.py` (PPM input).
- Produces: `make ios-app-check` — launches with `-- --frames 300 --screenshot Documents/shot.ppm`, waits for `mcfm: 300 frames rendered` in the console, copies `Documents/shot.ppm` out (`devicectl device copy from --domain-type appDataContainer --domain-identifier io.github.d4yvid.mcfm.ios`), runs `check_screenshot.py` (title screen, logo region) and checks the pixel size is the screen's (2868×1320).

- [ ] **Step 1: Write the check script** with the expected outcomes (frames line, screenshot present, size 2868×1320, `check_screenshot.py` exit 0).
- [ ] **Step 2: Run** `make ios-app-check` → FAIL (`--frames` not supported yet).
- [ ] **Step 3: Implement** `--frames/--screenshot` in `launcher.mm` (read pixels with `glReadPixels` from the bound FBO after the last frame, write PPM, log `mcfm: N frames rendered`, `exit(0)`).
- [ ] **Step 4: Run** `make ios-app-check` → PASS.
- [ ] **Step 5: Hand checklist with the owner** (recorded in the ledger): title screen full screen with no status bar; Play → Create New World → a world (touch, D-pad); home and back (saved, draws again); lock and unlock; type a world name (soft keyboard, return ends editing); Choose New Skin (photo picker); sound plays; rotate the phone between the two landscapes.
- [ ] **Step 6: Commit** — `iOS: on-device check (make ios-app-check)`.

### Task 6: Docs

- [ ] LAUNCHER.md: Stage 4 (iOS app) with its acceptance; research notes in `docs/research/ios-app.md` (dyld on iOS, what stays real, EAGL/FBO, lifecycle, signing); HANDOFF (state table, commands, pitfalls); CLAUDE.md (commands); `ios/README.md` (the app first, the IPA mod as legacy).
- [ ] Commit: `Stage 4: our own iOS app`.

## Acceptance

- `make ios-app IPA=…` builds a signed `dist/ios/mcfm.app` (and `.ipa`) with the game bundled, from the command line.
- `make ios-app-check` passes on the owner's iPhone: the engine starts at 2868×1320, 300 frames render, the screenshot shows the title screen.
- The hand checklist passes (world by touch, lifecycle with saving, keyboard, skin picker, sound).
- `make test` is green; the Android and macOS checks still pass.
