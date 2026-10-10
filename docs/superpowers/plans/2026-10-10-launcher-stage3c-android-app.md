# Stage 3c — Our Android App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An APK of our own (Kotlin app code, target SDK 37, minimum API 28, built by `make`
with the command-line tools, no Gradle) that:
- imports a user-supplied decrypted Minecraft PE 0.15.10 IPA;
- runs the iOS game image in a full-screen window, with touch, physical keyboard/mouse and
  text input;
- plays: a world can be created and rendered on the emulators.

**Architecture:**
- **Kotlin activities.** `ImportActivity` picks an IPA through the system file picker and
  unpacks `Payload/*.app/minecraftpe2` and `data/` into the app's storage. `GameActivity`
  shows a full-screen `SurfaceView`.
- **Native.** `libmcfm_launcher.so` (Stage 3b) gains a JNI entry point. On the device it:
  - converts the binary to the loadable image with a C++ port of `mcfm_image.py dylib --hooks`
    (`shared/loader/convert.cpp`);
  - checks the LC_UUID;
  - runs the engine on a render thread, with an EGL window surface on the `SurfaceView`'s
    `ANativeWindow`.
- **Events.** Kotlin forwards lifecycle (pause/resume/focus, surface size), touches (to
  `Multitouch::feed`), keys and mouse (to the shared `keyboard_mouse` module) and IME text (to
  the engine's text queue). The render thread applies them between frames.
- **Packaging.** The framework stubs are prebuilt at APK build time from a committed list of
  the game's import names (Apple API names only), and shipped in `lib/arm64-v8a/` with the
  launcher.
- **Signing.** A debug key generated locally (git-ignored).

**Tech Stack:** Kotlin 2.4.21 (kotlinc + kotlin-stdlib, dexed with d8), Android platform 37.0,
build-tools 37.0.0 (aapt2, d8, zipalign, apksigner), Temurin JDK 21, NDK r27d, C++ (launcher),
C++11 (`shared/`), make.

**Spec:** `docs/LAUNCHER.md` Stage 3 (item 5, acceptance) and its decisions: our own APK;
Kotlin app code; latest target SDK (37) with 16 KB pages; prebuilt APKs outside Google Play.

Research done for this plan (2026-10-10):
- **Touch:** iOS feeds `Multitouch::feed(button, state, x, y, pointer)` (`0x100020EFC`),
  with x and y in pixels and pointer 0–11:
  - down: `(1, 1, x, y, id)`;
  - move: `(0, 0, x, y, id)`;
  - up/cancel: `(1, 0, x, y, id)`.
- **Tools:** downloaded and verified: build-tools 37.0.0 (`android-37.0/`), platform 37.0 r02
  (`android-37.0/`), Temurin 21.0.12.1+1 (`jdk-21.0.12.1+1/`), Kotlin 2.4.21 (`kotlinc/`).

## Global Constraints

- Everything from 3a/3b holds:
  - no Mojang files committed;
  - Darwin numbers from `darwin_abi.h`;
  - only `libmcfm_launcher.so` carries our C++ runtime, and it exports only its C/JNI entry
    points;
  - 16 KB-aligned `.so` files, stored uncompressed in the APK and zipaligned to 16 KB
    (`zipalign -P 16`).
- The app never downloads or bundles game files: the user supplies a decrypted 0.15.10 IPA. The
  import refuses anything else (encrypted binary, wrong LC_UUID) with a clear message.
- Package `io.github.d4yvid.mcfm`, label "Minecraft PE (mcfm)", landscape, immersive full
  screen.
- `make test` needs no SDK. Device checks need a running emulator; the app check needs the IPA
  (`game-files/ios/minecraftpe2.ipa`).

## Review Focus

1. **Lifecycle:** pause/resume, screen off/on, and app switching while in a world must save on
   pause (`suspend`) and never draw to a destroyed surface. The render thread stops before
   `surfaceDestroyed` returns (Task 5 test: pause/resume cycles via `adb shell input keyevent`
   and `am` while the game runs, then the game still renders).
2. **Import robustness:** a wrong, truncated or encrypted IPA, or one with an unexpected
   layout, gives a message and leaves no half-imported game. A re-import replaces the old
   one atomically (Task 3 tests).
3. **Converter equivalence:** the C++ converter's output is byte-identical to `mcfm_image.py
   dylib --hooks` (on fixtures in `make test`; on the game when present).
4. **Touch coordinates:** pixels in the surface's coordinate space, pointer ids stable per
   finger and limited to 0–11. Multi-touch (2 fingers) works (Task 4 test with
   `input motionevent` / a native injection test).
5. **Threading:** JNI calls arrive on the UI thread. Engine calls happen only on the render
   thread, through a lock-free or locked event queue, never directly from the UI thread.

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/android/fetch_sdk.sh` (+ build-tools, platform), `tools/android/fetch_jvm_tools.sh` | JDK 21, kotlinc into `~/Library/Android/jvm-tools/` (pinned, checksummed) |
| `shared/loader/convert.{h,cpp}`, `shared/tests/convert_test.cpp` | C++11 port of the executable→dylib conversion and hook patching |
| `android/launcher/game_imports.tsv` | the game's import names (lib, symbol, kind): stubs prebuilt for the APK |
| `android/launcher/app.cpp` | JNI: import (convert + check), surface created/changed/destroyed, pause/resume/focus, input events; the render thread and its event queue |
| `android/app/AndroidManifest.xml`, `android/app/res/…`, `android/app/kotlin/io/github/d4yvid/mcfm/{ImportActivity,GameActivity,GameView,Native}.kt` | the app |
| `tools/android/build_app.sh` + `make android-app` | aapt2, kotlinc, d8, zip, zipalign -P 16, apksigner → `dist/android/mcfm.apk` |
| `tools/android/app_check.sh` + `make android-app-check` | install, import the IPA (debug intent), launch, screenshots, taps, lifecycle cycles |
| docs | LAUNCHER.md 3c ☑ and Stage 3 ☑, research, HANDOFF, CLAUDE.md, android/README.md |

Branch: `claude/android-3c` (from `main`).

---

### Task 1: Toolchain for the app

**Interfaces:**
- `fetch_sdk.sh` gains `build-tools/37.0.0` and `platforms/android-37.0`.
- `fetch_jvm_tools.sh <dir>`: Temurin JDK into `<dir>/jdk-21` and Kotlin into
  `<dir>/kotlinc`, with SHA-256 checks and the same overrides and tests as `fetch_sdk`.
- Make variables: `JAVA_HOME`, `KOTLINC`, `BUILD_TOOLS`, `ANDROID_JAR`. Target `android-app-sdk`
  fetches both.

- [ ] Test first: `fetch_sdk_test.sh` gets cases for the JVM-tools script (good tarball,
  wrong checksum, wrong top directory). RED.
- [ ] Implement; GREEN; install for real; `kotlinc -version` and `aapt2 version` run.
- [ ] Commit.

### Task 2: The converter in C++

**Interfaces:**
`mcfm::loader::convert_executable(const std::vector<uint8_t> &in, const std::vector<Hook> &hooks,
std::vector<uint8_t> *out, std::string *error)`, the same transformation as `cmd_dylib`:
- `MH_EXECUTE` becomes `MH_DYLIB`, plus an `LC_ID_DYLIB`;
- `__PAGEZERO` becomes `__MCFM_PAD`;
- `__objc_*` sections are renamed `__xbjc_*`;
- framework load commands point to `@rpath/mcfm_stub_<lib>.dylib`;
- the platform is retagged macOS;
- hooks are patched (`adrp/ldr/br x16`) with the table at the tail of `__DATA`.

It also checks `cryptid == 0` and the arm64 slice. FAT binaries are thinned, as
`thin_arm64` does.

- [ ] Test first, `convert_test.cpp` (host, `make test`): on the loader fixture executable
  (`loader_fixture.sh`) the output equals `mcfm_image.py dylib --hooks` byte for byte. Also
  refused, each with an error message:
  - an encrypted binary (crafted `cryptid = 1`);
  - a non-arm64 binary;
  - a truncated file.

  On the real game (when `GAME` is set), `make android-convert-check` compares with
  `dist/launcher/libminecraftpe.dylib`. RED.
- [ ] Port; GREEN. Commit.

### Task 3: JNI entry points and the import

**Interfaces** (`app.cpp`, JNI on `io.github.d4yvid.mcfm.Native`):
- `nativeImport(binaryPath, outDir): String?`:
  1. reads the extracted `minecraftpe2`;
  2. checks the UUID;
  3. converts with the seams' hooks;
  4. writes `outDir/minecraftpe.dylib.tmp`, renames it atomically;
  5. returns an error message, or null on success.
- `nativeStart(filesDir, homeDir)`, `nativeSurface(surface | null, width, height)`,
  `nativePause/Resume/Focus(bool)`, `nativeTouch(action, pointer, x, y)`,
  `nativeKey(vk, down)`, `nativeMouse(…)`, `nativeText(String)`.
- `launcher.map` exports `JNI_OnLoad` and `Java_io_github_d4yvid_mcfm_Native_*` only.

- [ ] Test first: an NDK test `app_import_test` drives `nativeImport`'s C++ core (not the JNI
  wrapper). It covers:
  - the fixture binary, imported and loadable;
  - an encrypted one, refused;
  - a re-import, replacing the old image atomically (the old file stays valid until the
    rename).

  RED.
- [ ] Implement; GREEN. Commit.

### Task 4: The game on a window, with input

**Interfaces:**
- **Render thread** (`app.cpp`):
  - waits for a surface;
  - on the first surface: loads the image (hooks, UUID), creates the EGL window surface and
    GLES 3 context, starts the engine at the surface size;
  - per frame:
    1. drains the event queue (touch → `Multitouch::feed`; keys and mouse →
       `keyboard_mouse`; text → the engine's keyboard text queue, as macOS does);
    2. calls `engine.frame()`;
    3. drains the main dispatch queue;
    4. calls `eglSwapBuffers`;
  - surface changed → `engine.resize`;
  - surface destroyed → the EGL surface is released (the context is kept) before the call
    returns;
  - pause → `focus_lost` + `suspend`; resume → `resume` + `focus_gained`.
- **Input mode:**
  - touch is the default (`getDefaultInputMode` → touch on Android);
  - a physical mouse switches to mouse mode, as the Win10 edition does on tablets;
  - research first how the engine switches; fallback: the mode follows the last device used.
- **Text:** `showKeyboard`/`hideKeyboard` (AppPlatform slots 9/10, `KeyboardCallbacks`) open and
  close the soft keyboard through a JNI callback to Kotlin. Committed text and backspace come
  back as `nativeText`.
- Small parameters (bool/char) passed to game functions are widened to 32 bits (research
  note).

- [ ] Test first, an NDK test of the event queue and the touch mapping (pure C++):
  - pointer ids map to 0–11, with a free slot reused after an up;
  - action codes map to the iOS feed triples;
  - two simultaneous fingers;
  - text and backspace map to the text queue's `{string, bool}` entries.

  RED, then implement, GREEN.
- [ ] Implement the render thread and the Kotlin `GameView`/`GameActivity`. The device check is
  in Task 5.
- [ ] Commit.

### Task 5: The APK and the device check

**Interfaces:**
- `make android-app` → `dist/android/mcfm.apk`:
  - resources through aapt2;
  - Kotlin to classes, then d8 with kotlin-stdlib;
  - `lib/arm64-v8a/`: `libmcfm_launcher.so`, `libmcfm_stubrt.so` and the stubs built from
    `game_imports.tsv`;
  - `zipalign -P 16 -f 4`, then `apksigner` with `build/android/debug.keystore` (generated
    with keytool).
- `make android-app-check` (API 37, then API 28):
  1. install, push the IPA;
  2. `am start` `ImportActivity` with the debug-only extra `path` (honored only when the app is
     debuggable), then wait for "imported";
  3. start `GameActivity`, wait, `screencap` → the title screen check (`check_screenshot.py`);
  4. tap Play, then Create New, then Create World (coordinates scaled from 1280×720), wait,
     `screencap`;
  5. the in-world check: not the menu, many colors, the hotbar region at the bottom centre;
  6. lifecycle: HOME, then relaunch, then screencap again (still renders); `options.txt` and a
     world exist under the home directory;
  7. logcat has no `mcfm: terminate` / FATAL lines.
- `apksigner verify`, `zipalign -c -P 16`, `aapt2 dump badging` (targetSdk 37, minSdk 28).

- [ ] Test first: `app_check.sh` fails (no APK). Then build and iterate on the emulator. Every
  crash or wrong behaviour gets a unit test or fixture first.
- [ ] GREEN on API 37 and API 28; look at the screenshots.
- [ ] Commit.

### Task 6: Docs

- [ ] Docs:
  - LAUNCHER.md: 3c ☑ and Stage 3 ☑ with the acceptance numbers;
  - research: input mode, IME, lifecycle findings;
  - HANDOFF: state and commands;
  - CLAUDE.md: the commands;
  - `android/README.md`: the new app, with the old mod's section kept as legacy.
- [ ] Commit: "Stage 3c: our Android app imports the IPA and plays".

## Acceptance

- `make android-app` builds a signed, 16 KB-aligned APK (target SDK 37, min SDK 28) with
  Kotlin app code.
- `make android-app-check` passes on the API 37 (16 KB pages) and API 28 emulators:
  - the IPA is imported on the device;
  - the title screen renders in a window;
  - touch taps create a world, and it renders;
  - the game survives pause/resume and saves.
- `make test` is green, and the macOS launcher still passes `make check`.
