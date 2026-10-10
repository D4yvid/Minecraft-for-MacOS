# Minecraft for macOS — notes for Claude

Mods for Minecraft PE 0.15.10 on macOS (the Mach-O launcher; Catalyst deprecated), iOS and
Android from one C++11 core.
Read docs/HANDOFF.md (state, knowledge base, roadmap, pitfalls) and docs/ARCHITECTURE.md first.
Reverse-engineering notes: docs/research/. Local game files and the IDA database: game-files/ (git-ignored, see docs/GAME_FILES.md); idalib venv: .venv/.

## Commands
- `make test` — all host tests (no game files). Run before every commit.
- `make app` / `check` / `run` (= `launcher` / `launcher-check` / `launcher-run`) — the macOS
  build: the Mach-O launcher (docs/LAUNCHER.md); needs `make angle` once (downloads ANGLE).
  `check` renders 120 frames; stub census in build/launcher/census.txt.
- `make catalyst` / `catalyst-check` / `catalyst-run` — the **deprecated** Mac Catalyst build;
  `catalyst-check` launches it for 15 s (`make catalyst` refuses while it runs — don't kill the
  user's game).
- Engine code in the launcher image is patched only at conversion (`mcfm_image.py dylib --hooks`,
  hooks in `shared/launcher/seams.cpp`); never write to its `__TEXT` at runtime.
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
- The owner may be playing: `make app`/`check`/`run` refuse while `mcfm-launch` runs;
  `make catalyst-check` closes a running Catalyst game (check `pgrep -x minecraftpe` first).
