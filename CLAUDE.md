# Minecraft for macOS — notes for Claude

Mods for Minecraft PE 0.15.10 on macOS (Catalyst), iOS and Android from one C++11 core.
Read docs/ARCHITECTURE.md first.

## Commands
- `make test` — all host tests (no game files). Run before every commit.
- `make app` / `make check` / `make run` — macOS build from `GAME` in config.mk; `check`
  launches the game for 15 s (`make app` refuses while it runs — don't kill the user's game).
- `make android` — NDK r10c under Rosetta; `make android-apk APK=…`
- `make ios` needs Xcode; without it `make ios-syntax` (part of `make test`) compiles iOS code.

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
