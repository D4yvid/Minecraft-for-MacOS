# Handoff: Minecraft for macOS — state, knowledge base, roadmap

For the next agent (or person) picking this up. Read this, then
[ARCHITECTURE.md](ARCHITECTURE.md), then [CLAUDE.md](../CLAUDE.md). Facts are marked
✅ verified (and how) or ❓ assumed / unverified.

## 1. Mission

Turn Minecraft: Pocket Edition **0.15.10** (one game build, three platforms) into a
first-class desktop game on Apple Silicon Macs, and share the work with iOS and Android,
by modding the shipped engine — never redistributing it. Next goals set by the owner:

0. **Mach-O launcher** — load the iOS binary ourselves, never run its iOS glue, and drive the
   engine from our own launcher on macOS, then Android (target SDK 37, our own APK) and other
   arm64 hosts. Plan and
   status: [LAUNCHER.md](LAUNCHER.md); Stage 0 survey done
   ([research/macho-launcher.md](research/macho-launcher.md)). Goals 1–2 below become parts of it.
1. **Generic AppPlatform** — our own platform layer instead of patching the iOS/Android ones
   slot by slot ([§6.1](#61-generic-appplatform)).
2. **Our own renderer** — replace or wrap the OpenGL ES backend ([§6.2](#62-renderer)).
3. **A real module system** — features as modules with enable/disable and config (§6.3).
4. **Feature completeness** and **bug fixing** (§6.4, §6.5).

## 2. Current state (2026-10-10, `main` = `7992a2f`, public repo D4yvid/Minecraft-for-MacOS)

| Area | State | Verified how |
|---|---|---|
| macOS launcher (default, `make app`) | iOS binary loaded by our own launcher: no Catalyst/UIKit, every framework stubbed; ANGLE/Metal window, keyboard, raw mouse look, text entry, sound, no Xbox prompt | ✅ `make test`; `make check` (120 frames); owner played a world 2026-10-10 |
| macOS Catalyst (deprecated, `make catalyst`) | Win10 UI, keyboard+mouse with pointer capture, resizable window that the engine follows, auto-hiding/fading title bar, App Store receipt prompt skipped | ✅ `make check` (bundle test + 15 s launch with log asserts); owner's screenshots (Win10 title screen, game filling the window). Hand checks M1–M7 below **not** all confirmed |
| iOS | Win10 UI + receipt skip dylib, `make ios-ipa` → unsigned IPA | ✅ compiles for iOS (`make ios-syntax`), IPA structure test with a stub dylib. ❓ never linked against the real iOS SDK (no Xcode here), never run on a device |
| Android app (`make android-app`) | Our own Kotlin APK (target SDK 37, min 28, 16 KB pages): imports the user's decrypted IPA, runs the iOS image with our loader over a Darwin libSystem layer and an Apple-ABI libc++; GLES 3 window, touch GUI, soft keyboard, a mouse and keys, FMOD on AAudio, saves in the background | ✅ `make android-app-check` on the Android 17 and 9 emulators (import, title screen, a world created by touch, background/resume); `make android-test`. ❓ never run on a physical device |
| Android, legacy mod | Win10 UI via `libmcfm.so`, `make android-apk` (patches Mojang's APK; replaced by the app above) | ✅ builds with NDK r10c, library + APK pipeline tests on stand-in APKs. ❓ never run on a device |
| Shared core | `Platform` interface, `win10_ui`, `keyboard_mouse`, input logic | ✅ host tests (C++11), ASan/UBSan clean |
| Repo hygiene | Apache-2.0, no Mojang files, `make test` from a fresh clone | ✅ `no_game_files_test`, fresh-clone run |

### Hand checks still open (macOS — ask the owner or do them with screen access)
M1 resize/full screen fills the window · M2 Win10 layout (✅ seen in a screenshot) · M3 menu
hover/click once, mouse look direction/speed (`kLookScale` in `macos/src/mac_input.mm`) ·
M4 WASD/Space/Shift/clicks/1–9/scroll/E/Esc · M5 typing in chat doesn't move the player ·
M6 Cmd-Tab releases the cursor and keys · M7 title bar fades in from the top-left corner and
stays while the pointer is on it; still OK after full screen. No system beep on key presses.

## 3. Getting set up

### Machine (owner's Mac, Apple Silicon, macOS 27)
- Xcode Command Line Tools only (no Xcode → no iPhoneOS SDK). Python 3 (Homebrew).
- IDA Professional 9.4 at `/Applications/IDA Professional 9.4.app` (idalib available).
- NDK r10c at `~/Library/Android/ndk/android-ndk-r10c`. Its installer is a **32-bit** binary
  (won't run on modern macOS, not even under Rosetta): it was unpacked with
  `7zz x -snld …bin`, and 9 chained symlinks (incl. `arm-linux-androideabi-4.9/…/bin/ld`) had
  to be recreated by hand. `ndk-build` must run under `arch -x86_64` (the Makefile does).
- `brew install apktool` (pulls OpenJDK; `keytool`/`jarsigner` live in `$(brew --prefix openjdk)/bin`).
- `gh` logged in as D4yvid.

### Game inputs (never committed)
Everything lives in the git-ignored **`game-files/`** — layout, provenance and how to
rebuild it: [GAME_FILES.md](GAME_FILES.md). The Makefile uses it by default (`GAME`, `APK`).
- iOS: decrypted 0.15.10 app, IPA, thin arm64 binary (arm64 UUID
  `01DFB489-A881-3BDD-8F98-6F016E409625`); the owner's source copy is
  `~/Downloads/Payload/minecraftpe2.app`.
- IDA database: `game-files/ida/minecraftpe2-arm64.i64` (auto-analysed).
- Android: `libminecraftpe.so` with symbols, decompiled tree, and **stand-in** APKs rebuilt
  from an earlier injection project's decompiled APK — no original Android APK on the machine; ask the owner
  for one before Android device work.

### idalib (scripted IDA)
`.venv/` (git-ignored) is set up with idalib:
```bash
.venv/bin/python -I tools/ida/q.py game-files/ida/minecraftpe2-arm64.i64 d 0x10070f670
```
Recreate it with the commands in [tools/ida/README.md](../tools/ida/README.md).
`idat` with `-S script.py` does **not** work here (IDAPython not configured for idat) — use
idalib. Re-creating the database: `make game-files IOS=<app> IDA=1` (~2 min).

### Daily commands
```bash
make test        # all host tests, no game files (~1 min)
make angle           # once: ANGLE from the pinned Electron release (~130 MB download)
make app             # = launcher: dist/launcher (converted image + stubs + ANGLE + mcfm-launch)
make run             # = launcher-run: plays the game in a window (MCFM_LOOK_SCALE tunes turning)
make check           # = launcher-check: renders 120 frames; census in build/launcher/census.txt
                     #   run/check use our own loader; LOADER=dyld uses Apple's
make loader-check    # our loader vs dyld on the game: every fixup location compared
make catalyst        # deprecated Catalyst build; refuses while that game runs
make catalyst-check  # bundle/IPA/UUID tests + 15 s launch — CLOSES a running Catalyst game
make catalyst-run
# The Catalyst build is a deprecated build mode until the launcher replaces it (LAUNCHER.md).
make android     # libmcfm.so + tests       make android-apk APK=…   make ios-ipa
# Android launcher (Stage 3): toolchain once, then an emulator (or a device over adb)
make android-sdk llvm-runtimes   # NDK r27d, adb, emulator, API 37/28 images; LLVM sources
make android-emulator [API=28]   # boots the API 37 (16 KB pages) or API 28 emulator headless
make android-test                # runtime, Darwin layer and loader tests on the device
make android-boot-check          # the converted game (make app) initializes on the device
make android-frames-check        # ... boots, renders 120 frames, screenshot in build/android-launcher
make android-app-sdk             # once: JDK 21, kotlinc, build-tools/platform 37 (for the app)
make android-app                 # dist/android/mcfm.apk (Kotlin, aapt2, d8, zipalign, apksigner)
make android-app-check IPA=…     # clean install, import, title, new world by touch, resume
```

## 4. How it works (beyond ARCHITECTURE.md)

### Loading
- macOS: `macos/tools/convert.sh` copies the decrypted app, thins to arm64, retags every
  Mach-O to Mac Catalyst (`vtool -set-build-version maccatalyst 11.0 14.0`), deletes
  `UIRequiresFullScreen` (else the window can't resize), renames `minecraftpe2` →
  `minecraftpe` + display name "Minecraft PE" (bundle id `com.mojang.minecraftpe2` kept:
  settings live in `~/Library/Preferences/com.mojang.minecraftpe2.plist`), injects
  `LC_LOAD_DYLIB @executable_path/Frameworks/libmcfm.dylib`, clears xattrs (quarantine /
  provenance → AMFI SIGKILL otherwise) and signs ad hoc. Worlds are in
  `~/Documents/games/com.mojang/minecraftWorlds` (outside the bundle).
- Entry points run as load-time constructors / `JNI_OnLoad`, **before the game's `main`**.

### Engine facts (iOS addresses unslid; see `shared/apple/addresses_0_15_10.h`)
- ✅ `AppPlatform*` singleton `0x100F5E850`; `AppPlatform_iOS` vtable `0x100EABE00`;
  full named slot map: [research/appplatform-vtable.md](research/appplatform-vtable.md)
  (103 slots, 53 overridden by iOS; names from Android, cross-checked).
- ✅ iOS `AppPlatform` fields seen in slot bodies: `+8` pointer focus, `+9` keyboard visible,
  `+328/+332` UI-scaling override value/flag, `+408/+432/+456/+480` path strings
  (external/temp/internal/userdata), `+504` the `minecraftpeViewController` (ObjC).
- ✅ `MinecraftClient` (the `_app` ivar of `minecraftpeViewController`) vtable names:
  [research/minecraftclient-vtable.md](research/minecraftclient-vtable.md) (19 `update`,
  20 `setUISizeAndScale`, 21 `setRenderingSize`).
- ✅ Input (identical layout on armv7/arm64): `Keyboard::_inputs` `std::vector<{int32 state;
  u8 key}>` `0x100F59FF8`, `Keyboard::_states` `int32[256]` `0x100F59BF8` (Windows VK codes,
  Enter = 13), text input `std::vector<{std::string; bool}>` `0x100F5A010` (used by the iOS
  `ShowKeyboardView`; **not** fed by the mod yet), `Mouse::_instance` `0x100F5A040` with
  16-byte `MouseAction{x,y,dx,dy,btn,data}` queue at `+0x18`, `MouseDevice::feed(dev,btn,
  state,x,y)` `0x1000201BC`. Mouse buttons 0 move/1 L/2 R/3 M/4 wheel. Keyboard and mouse
  mappers are always constructed (`sub_100017854`); `InputHandler+200` = input mode
  (1 mouse/2 touch/3 gamepad), initialised from `AppPlatform::getDefaultInputMode` (slot 96).
  The game drains and resets both queues every tick.
- ✅ Win10 UI = `getEdition`→"win10" (93), `getPlatformUIScalingRules`→0 (99) on iOS; Android
  also needs `useCenteredGUI` (68), `getPlatformType`→0 (69), `useMetadataDrivenScreens`
  (66). **Apple does not map 66/68/69 yet although the slots are now known** (quick win).
- ✅ Pointer: the game calls `hideMousePointer` (13) / `showMousePointer` (14) when it grabs /
  releases the mouse; macOS turns these into CoreGraphics capture (`pointer_lock.mm`).
- ✅ `minecraftpeViewController` (ObjC, names intact): `initView` (viewScale + sizes),
  `drawFrame` (frame), `width`/`height` (were `UIScreen` based → now window based),
  `touchesBegan/Moved/Ended/Cancelled` → `Multitouch::feed` (`0x100020EFC`), `showKeyboard:`,
  `getKeyboardHeight`, `pickSkinImage:`. `EAGLView`: `layoutSubviews` deletes the framebuffer,
  `setFramebuffer` recreates it. `StoreManager initWithStoreListener:` starts an
  `SKReceiptRefreshRequest` when there is no receipt → "Sign in with your Apple Account"
  (skipped by `shared/apple/store.mm`).
- ✅ Android (`libminecraftpe.so` keeps symbols): AppPlatform class in use is
  `AppPlatform_android23` (Android 6+). Its `getDefaultInputMode` is
  `_ZNK19AppPlatform_android19getDefaultInputModeEv` (not mapped in `android_platform.cpp` yet).
  `Mouse::feed` = `_ZN5Mouse4feedEccssss`. ABI: armeabi-v7a (softfp) with gnustl; a `std::string`
  return goes through r0 (sret) with `this` in r1 — verified on the compiled replacement.

## 5. Conventions (keep them)

- Shared code is **C++11**, platform-free, compiled with each platform's STL (gnustl on
  Android, libc++ on Apple). Host tests build it with `-std=c++11`.
- New engine access: `engine.h` enum → map per platform (`address_platform.mm` offsets /
  `android_platform.cpp` symbols) → test with `shared/tests/fake_platform.h`.
- Apple addresses only behind the LC_UUID guard; Android behind `AndroidPlatform::valid()` and
  `check_apk_lib.sh`.
- Entry points: function-local statics only (see pitfalls).
- TDD: a test that fails first for every behaviour change and bug fix; `make test` green before
  committing; `make check` for macOS changes when the game isn't running.
- Never commit Mojang files; never kill the owner's running game (`make app` refuses).
- Work on a branch, fast-forward `main`, push (the owner has approved pushing `main`;
  never force-push without asking). Commit trailer: `Co-Authored-By: Claude …`.
- Logs: `mcfm:` prefix (NSLog / `adb logcat -s mcfm`).

## 6. Roadmap

### 6.0 Mach-O launcher
The owner's main direction since 2026-10-10. See [LAUNCHER.md](LAUNCHER.md) (stages, tasks,
acceptance, decisions). The Catalyst build is now a deprecated build mode (`make catalyst`).
Stage 0 found the engine uses no Apple framework at all. Stage 1a is done: the game image
loads in a plain macOS process with every framework and libobjc stubbed (`make launcher`,
`make launcher-check`). Stage 1b is done: our AppPlatform, the engine boot and an ANGLE/Metal
window render the Win10 Edition title screen. **Stage 1 landed (2026-10-10):** with 1c the
launcher is playable (keyboard, raw mouse, text, sound, no Xbox prompt; the owner played a
world) and `make app`/`run`/`check` use it. **Stage 2 landed (2026-10-10):** our own Mach-O
loader (`shared/loader/`, macOS layer `macos/launcher/loader_macos.cpp`) runs the game by default
and matches dyld on every fixup (`make loader-check`). **Stage 3a landed (2026-10-10):** on
Android 17 and 9 emulators the same loader maps the game and all its initializers run, over a
Darwin libSystem layer (`android/launcher/darwin/`) and an Apple-ABI libc++ runtime
(`android/launcher/runtime/`). **Stage 3b landed (2026-10-10):** the engine boots there with the
launcher's AppPlatform and renders the title screen (GLES 3, FMOD on AAudio; `make
android-frames-check`). **Stage 3c landed (2026-10-10), Stage 3 done:** our own Kotlin APK
(`android/app/`, built by `make android-app` without Gradle) imports the user's IPA on the device
and plays: window, touch, soft keyboard, lifecycle (`make android-app-check` on Android 17 and 9).
Findings in research/macho-launcher.md and research/android-launcher.md. Next candidates: a
physical-device run, world export/import in the app, the generic AppPlatform (6.1).

### 6.1 Generic AppPlatform
Goal: one shared, platform-neutral `AppPlatform` behaviour definition instead of ad-hoc slot
patches, so features don't depend on what iOS/Android happened to implement.

Facts that shape it: the engine reaches the platform only through the singleton + virtual
calls (✅ 319 code references to the singleton in IDA; ❓ only a sample was inspected, those
were all virtual calls); base-class slot order is identical on
iOS and Android (✅); platform objects carry fields the engine reads directly via
non-virtual helpers (✅ `+328/+332` read by `sub_100460A64`), so **replacing the object** is
riskier than **replacing its vtable**.

Suggested steps:
1. Represent the whole vtable in shared code: an `engine::Slot` (or a generated table from
   `research/appplatform-vtable.md`) for all 101 base slots, with each platform mapping slot
   → location (Apple: byte offset; Android: index) — the map makes this mechanical.
2. Quick wins on the way: map 66/68/69 on Apple; map `getDefaultInputMode` on Android.
3. Introduce a shared `GenericAppPlatform` policy layer: for each slot, "keep platform",
   "override with shared implementation", or "forward to platform glue" (e.g. keyboard,
   clipboard, file paths), installed by patching the live vtable (current mechanism).
4. Only then consider a full replacement vtable (copy the platform vtable, patch in place,
   swap the vptr once) — gives one atomic install and per-slot originals.
Acceptance: every patched slot has a host test via FakePlatform; macOS behaviour unchanged.

### 6.2 Renderer
See [research/renderer.md](research/renderer.md): `mce::` abstraction with a compile-time
OpenGL backend (no vtables), 91 imported `gl*` functions on iOS, EAGLView owns the
framebuffer. Suggested: instrument first (frame timing, GL call census), then choose between
rebinding the GL imports to our own GLES-on-Metal (or ANGLE) and inline-hooking `mce::*OGL`.
Prerequisite for both B-style hooks on Apple: an inline hooking library (none in the repo).

### 6.3 Module system
Deferred by the owner ("later we'll build a better module system"). The old injection
project's registry was deliberately removed; today features are `install()` functions called from the
entry points. Requirements gathered so far: per-platform availability, enable/disable at
runtime, persisted config, ordering/dependencies, C++11. Design it with the owner first.

### 6.4 Feature completeness (candidates)
- Text input through `Keyboard` text vector (`0x100F5A010`) for desktop text boxes instead of
  the iOS `ShowKeyboardView` popup.
- Keyboard + mouse on iPad (UIKit `prefersPointerLocked`, no CoreGraphics on iOS) and on
  Android (physical mice/keyboards; `getDefaultInputMode` now named).
- In-game settings for the mod (needs a UI hook; the old injection project had an Options-screen toggle).
- Clipboard, full-screen key, controller support, scroll/look sensitivity settings.
- Xbox Live / Realms are not functional in these copies; decide whether to hide them.

### 6.5 Bugs / risks to look at
- First launch after each `make app` sometimes exits ~5 s in after losing focus, no crash
  report (seen with and without the receipt prompt; likely a one-time macOS prompt for the
  re-signed app — ❓ unconfirmed). `make check` reruns usually pass.
- Mouse look sign/speed (`kLookScale`, dy sign) and trackpad scroll units
  (`MCFM_LOG_SCROLL=1` logs raw values) are untuned.
- Title bar fade can jump if a resize happens mid-fade.
- iOS mod requires iOS 15+ (libc++ floor) though the game runs on older iOS.
- Android: only `AppPlatform_android23` (Android 6+); never run on a device.
- `macos/tests/smoke.sh` (`make check`) starts with `pkill -x minecraftpe`, so it closes the
  owner's running game; make it refuse like `convert.sh` does.

## 7. Pitfalls we already hit (save yourself the time)

- **Load-time constructor vs C++ globals**: `__attribute__((constructor))` ran before the
  dylib's own C++ globals were initialised → null vptr → SIGSEGV at launch. Use
  function-local statics; keep other globals constant-initialised (`constexpr` ctors).
- **Catalyst**: quarantine/provenance xattrs → SIGKILL (exit 137); `UIRequiresFullScreen` →
  fixed window; `UIScreen.bounds` is the display, not the window.
- **NSLog from the launched binary** shows in stdout/stderr when run directly
  (`dist/minecraftpe.app/minecraftpe`), not reliably in `log show`.
- **zsh**: a loop variable named `path` clobbers `$PATH`; `--include=*.h` globs fail
  ("no matches found") — quote them.
- **bash `set -o pipefail` + `grep -q`** gives false failures (SIGPIPE / exit status of the
  producer) — capture output into a variable, then grep.
- **make 3.81** compares whole seconds — timestamp-sensitive tests must set mtimes explicitly.
- **ASan on host tests**: raw writes into a `std::vector`'s spare capacity trip libc++
  container-overflow annotations; run with `ASAN_OPTIONS=detect_container_overflow=0`.
- **apktool 3** chokes on apktool-2 `build/` caches (duplicate lib entries) — delete them.
- **Android `dlsym` of a missing symbol returns NULL**; scanning a vtable for NULL matches the
  offset-to-top word — always use `vtable_scan.hpp`.
- The owner may be playing: check `pgrep -x minecraftpe` before `make app` (refuses) and
  before `make check` (would close the game).
- **Android app**: no `INTERNET` permission → `socket()` EPERM and RakNet's null peer crashed the
  Play screen; an app's stdout/stderr go nowhere (forwarded to logcat, tag `mcfm`);
  `Android/data` is unreachable by adb/`run-as` on Android 11+ (worlds are in `files/home`);
  `adb exec-in run-as … 'cat > f'` truncates big files (push to `/data/local/tmp`, `run-as cp`).
- **Darwin variadic ABI**: a test that calls a variadic `mcfm_darwin_*` shim from NDK code
  passes the arguments in registers, the shim reads the stack: call the `_impl` function.

## 8. Open questions for the owner

- Which platform leads the generic AppPlatform work (macOS first, as so far?).
- Renderer target: Metal on Apple only, or one backend for all (Vulkan/ANGLE)?
- Module system expectations (in-game UI? config file? hot reload?).
- A physical Android device (arm64, Android 9+) for an on-device run of the app.
- Install Xcode to link and sideload the iOS build?
- Old public commits still contain `/Users/dayvid/...` paths and the author email; history
  rewrite was recommended against (owner hasn't decided).

## 9. History

Design docs and plans live in `docs/superpowers/` (macOS mod, then the multi-platform
restructure, with the decisions taken during implementation at the end of each spec).
