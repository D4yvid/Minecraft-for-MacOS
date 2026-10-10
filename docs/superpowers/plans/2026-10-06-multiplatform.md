# Multi-platform Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the macOS mod repo into `Minecraft-for-MacOS` with a C++11 shared core used by macOS, iOS and Android builds of Minecraft PE 0.15.10, then publish it (Apache-2.0, public).

**Architecture:** `shared/` holds the Platform interface, Module/Client, engine vocabulary, pure input logic and two modules (Win10Ui, KeyboardMouse). `shared/apple/` implements Platform for stripped Mach-O builds via an address table. `macos/`, `ios/` and `android/` are thin glue: they construct a Platform, register modules, and own OS-specific input/windowing.

**Tech Stack:** C++11 (shared), Objective-C++17 (Apple glue, libc++), Android NDK r10c (gcc 4.9, gnustl, armeabi-v7a) under Rosetta, make, Python 3, bash.

**Spec:** `docs/superpowers/specs/2026-10-06-multiplatform-design.md`

## Global Constraints

- Shared code compiles with `-std=c++11` and includes no platform headers (no UIKit, Foundation, JNI, android/*).
- Never commit Mojang files: no `.ipa`, `.apk`, `.app`, `libminecraftpe.so`, `libfmod.so`, decompiled APK, game assets. `.gitignore` blocks them.
- macOS behaviour unchanged: host tests, `bundle_test`, smoke test keep passing after every task.
- Apple engine addresses only behind the LC_UUID guard (`01DFB489-A881-3BDD-8F98-6F016E409625`).
- Vtable patches are by `engine::Slot`, never raw byte offsets in shared code.
- License Apache-2.0; Substrate stays LGPL-3.0 in `android/third_party/substrate`; AOSP header kept.
- Log prefix `mcfm:` (macOS smoke greps updated accordingly).

## Review Focus

1. A platform that lacks a slot (e.g. UseCenteredGUI on Apple): module must skip it and still init the rest, not fail the whole client (Task 3 test).
2. Win10Ui `enabled()` false: AppPlatform must report the platform's original values, i.e. replacement must call through to the original (Task 3 test with FakePlatform originals).
3. Keyboard vector growth past capacity from shared code: must not corrupt the engine's vector (Task 3 test filling past capacity).
4. iOS IPA from a fat (armv7+arm64) binary: injected binary must remain launchable on arm64 devices (Task 5 structure test checks arm64 slice has LC_LOAD_DYLIB).
5. Android build without the user's `libminecraftpe.so`: clear error, not a cryptic link failure (Task 6).

---

### Task 1: Move to the new layout (no behaviour change)
- `git mv`: pure headers → `shared/include/mcfm/input/` (`keymap.h` + `keymap.cpp` → `shared/src/`, `input_policy.h`), Mach-O guard → `shared/apple/` (`macho_uuid.*`), host tests → `shared/tests/`, everything else in `src/` → `macos/src/`, `tests/{bundle,smoke}_test.sh` → `macos/tests/`, `tools/{convert.sh,inject.py}` → `macos/tools/` (inject.py → `tools/inject.py`, shared by iOS), RE scripts → `tools/ida/`.
- Top-level `Makefile` with `test`, `macos`, `app`, `run`, `check`, `clean`; `config.mk` optional include; `GAME` defaults from config.
- Verify: `make test` all pass; `make app GAME=<user app>`; `make check` (bundle + smoke) pass. Commit.

### Task 2: Shared core (Platform, Module, Client) + FakePlatform
- `shared/include/mcfm/{platform.h,module.h,engine.h}`, `shared/src/client.cpp`, `shared/tests/fake_platform.h`, `shared/tests/client_test.cpp`.
- Tests first: Client inits modules in order, returns false if any module init fails but still inits the others, logs each module name; Module names unique (duplicate add rejected).
- `engine.h`: `Slot`, `Global`, `KeyEvent` (static_assert 8 bytes), `MouseButton` constants.

### Task 3: Shared modules (Win10Ui, KeyboardMouse)
- Tests first against FakePlatform (fake vtable with original functions returning pocket values; fake `std::vector<KeyEvent>` and `int32_t states[256]`):
  - Win10Ui enabled → slots GetEdition/UIScalingRules/UseCenteredGUI replaced; calling them yields "win10"/0/true; enabled() flipping to false → calls return originals ("pocket"/2/false).
  - Missing slot → logged, other slots still patched, init returns true.
  - KeyboardMouse: DefaultInputMode → 1; Hide/Show call the callbacks; `key()` pushes KeyEvent and sets state; 1000 key events grow the vector correctly (begin/end/cap consistent, contents intact); `mouse_*` produce the right `mouse_feed` calls (wheel ±127, rel dx/dy, abs).
- Implement `shared/modules/win10_ui.{h,cpp}`, `shared/modules/keyboard_mouse.{h,cpp}`.

### Task 4: Apple platform + macOS on modules
- `shared/apple/address_platform.{h,mm}` from `engine.mm` + `addresses.h` (Slot→vtable offset, Global→address, mouse_feed). UseCenteredGUI not mapped on Apple (logged skip).
- `shared/apple/store.{h,mm}` (moved). macOS `main.mm` builds Client(AddressPlatform) + Win10Ui(always) + KeyboardMouse(callbacks → pointer_lock); `mac_input` calls the module API; delete `macos/src/platform.mm`, `engine.*`.
- Verify: `make test`, `make app`, `make check` (smoke expects `mcfm: patched`).

### Task 5: iOS target
- `ios/src/main.mm`: AddressPlatform + Win10Ui(enabled = iPad || `mcfm.win10ui`) + store skip. `ios/Makefile` fragment builds `build/ios/libmcfm.dylib` for `arm64-apple-ios12.0`.
- `ios/tools/make_ipa.sh <ipa|app> <out.ipa> <dylib>`: refuse non-0.15.10 / encrypted input; thin main binary to arm64; copy dylib to `Payload/*.app/Frameworks/`; inject `@executable_path/Frameworks/libmcfm.dylib`; strip `_CodeSignature`; zip.
- Test first: `ios/tests/ipa_test.sh` builds from the user's app and checks Payload structure, arm64-only, LC_LOAD_DYLIB present, dylib platform = iOS (vtool). Fails before tool exists.

### Task 6: Android import on the shared core
- Copy the old injection client `jni/` (minus `Libraries/Output/*minecraftpe*`), `Makefile`, `config.mk`, `APK.bat`, `.vscode` excluded; Substrate headers + `libsubstrate.a` → `android/third_party/substrate/` with LICENSE (LGPL-3.0).
- `android/jni/platform_android.{hpp,cpp}`: AndroidPlatform (VirtualTable on `_ZTV21AppPlatform_android23`, FindIndex by symbol, dlsym globals, `_ZN5Mouse4feedEccssss`).
- `Modules/Win10.UI.Module.cpp`: keep Options toggle + config; AppPlatform patches go through shared Win10UiModule with `enabled = isWindowsUIEnabled`.
- `Android.mk` adds shared sources + include path, links `-lminecraftpe` from `$(MCPE_LIB_DIR)` (config: `APK=`, extracted by `android/tools/extract_lib.sh`), error if missing.
- Verify: `make android` compiles + links with NDK r10c (Rosetta) against the user's `libminecraftpe.so`.

### Task 7: Repo polish
- `LICENSE` (Apache-2.0), `NOTICE`, `.gitignore` (Mojang files, build, dist, config.mk), `README.md` (what/requirements/build per platform/controls/legal), `docs/ARCHITECTURE.md`, `CLAUDE.md`, per-platform READMEs, `tools/ida/README.md`.
- Verify: `git ls-files` contains no Mojang files (script check), `make test` passes from a fresh clone.

### Task 8: Publish
- Final whole-branch review, fix pass, then push `main` to `github.com/D4yvid/Minecraft-for-MacOS` (public) after the user authenticates (`gh auth login`).
