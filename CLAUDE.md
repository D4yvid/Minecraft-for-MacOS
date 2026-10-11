# Minecraft for macOS — notes for Claude

Launchers for Minecraft PE 0.15.10 (the iOS arm64 image) from one C++11 core: macOS (the
Mach-O launcher; Catalyst deprecated), our own Android app and our own iOS app. The IPA mod
(`make ios-ipa`) and the Android APK patch (`make android-apk`) are legacy.
Read docs/HANDOFF.md (state, knowledge base, roadmap, pitfalls) and docs/ARCHITECTURE.md first.
Reverse-engineering notes: docs/research/. Local game files and the IDA database: game-files/ (git-ignored, see docs/GAME_FILES.md); idalib venv: .venv/.

## Commands
- `make test` — all host tests (no game files). Run before every commit.
- `make app` / `check` / `run` (= `launcher` / `launcher-check` / `launcher-run`) — the macOS
  build: the Mach-O launcher (docs/LAUNCHER.md); needs `make angle` once (downloads ANGLE).
  `check` renders 120 frames; stub census in build/launcher/census.txt. The game runs on our own
  Mach-O loader (`shared/loader/`, C++11, platform-free behind `LoaderOS`); `LOADER=dyld` uses
  Apple's. `make loader-check` compares both on the game's fixups (keep it at 0 differences).
- `make catalyst` / `catalyst-check` / `catalyst-run` — the **deprecated** Mac Catalyst build;
  `catalyst-check` launches it for 15 s (`make catalyst` refuses while it runs — don't kill the
  user's game).
- Engine code in the launcher image is patched only at conversion (`mcfm_image.py dylib --hooks`,
  hooks in `shared/launcher/seams.cpp`); never write to its `__TEXT` at runtime.
- `make android` — NDK r10c under Rosetta; `make android-apk APK=…` (the legacy Win10-UI mod)
- Android launcher (Stage 3): `make android-sdk llvm-runtimes` once, `make android-emulator`,
  then `make android-test` / `make android-boot-check` / `make android-frames-check` (need
  `make app`'s converted game). Everything built against our Apple-ABI libc++ lives in
  `libmcfm_launcher.so`, which exports no C++ symbols (Android's libc++ has the same names):
  never link the runtime into a binary that exports it. Game-facing constructors/destructors
  go through generated this-returning thunks (Apple arm64 C++ ABI).
  `android/launcher/darwin/` is the Darwin libSystem layer: Darwin numbers and layouts only
  from the generated `darwin_abi.h` (`make darwin-abi`), every import in `symbols.cpp` (sorted).
  Darwin variadic calls pass all variadic arguments on the stack (see `darwin/stdio.c`).
- Android app (Stage 3c, `android/app/`, Kotlin, no Gradle): `make android-app-sdk` once, then
  `make android-app IPA=…` (dist/android/mcfm.apk, a launcher with the game bundled: Mojang's
  files, never commit it; `android-app-debug`: the debuggable mcfm-debug.apk),
  `make android-app-run` (play in a windowed emulator) and `make android-app-check` on a running
  emulator (first start, title, new world by touch, lifecycle, re-conversion, the fatal path).
  IPA defaults to game-files/ios/*.ipa. The UI thread waits for the render
  thread only in onPause and surfaceDestroyed (`android/launcher/render_requests.h`). Logs: `adb logcat -s mcfm`;
  the app's files: `adb shell run-as io.github.d4yvid.mcfm`. One game per process: the render
  thread (`android/launcher/game_thread.cpp`) owns EGL and the engine; JNI only queues.
- iOS app (Stage 4, `ios/app` Swift + `ios/launcher` Objective-C++): `make angle-ios` once,
  `make ios-app IPA=…` (signed `dist/ios/mcfm.app` and `mcfm.ipa`, the game bundled: Mojang's
  files, never commit it; the iPhone must be connected, the profile is chosen for it),
  `make ios-app-run` / `ios-app-check` on the connected iPhone (unlocked). The image is loaded by dyld (iOS runs no
  unsigned code); all GL goes through ANGLE in `ios/launcher/egl_view.mm` (never link the
  system OpenGLES). Xcode at /Applications/Xcode.app is used through DEVELOPER_DIR.
- `make ios` (the legacy IPA mod) needs Xcode; without it `make ios-syntax` (part of
  `make test`) compiles iOS code.

## Rules
- Never commit Mojang files (`tools/tests/no_game_files_test.sh` enforces it).
- `shared/` is C++11 and platform-free; it is compiled with each platform's STL
  (gnustl on Android, libc++ on Apple) so `std::string` matches the engine.
- New engine access: add to `engine.h`, map in `shared/apple/address_platform.mm` and
  `android/jni/android_platform.cpp`, test against `shared/tests/fake_platform.h`.
- Apple addresses are only valid behind the LC_UUID guard; never read them before
  `AddressPlatform::attach()` succeeds.
- Load-time constructors run before C++ globals of the same image: use function-local
  statics in entry points.
- Features are plain `install()` functions for now; a module system is planned.
- Logs are prefixed `mcfm:`.
- The owner may be playing: `make app`/`check`/`run` refuse while `mcfm-launch` runs;
  `make catalyst-check` closes a running Catalyst game (check `pgrep -x minecraftpe` first).
