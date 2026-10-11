# Minecraft for macOS (and iOS, Android)

Launchers that run **Minecraft: Pocket Edition 0.15.10** — the iOS arm64 build of the game —
outside its own app: natively on Apple Silicon Macs with the Windows 10 (desktop) UI, keyboard
and mouse, and in our own Android and iOS apps. The iOS glue in the game binary is never run;
our launcher loads the image, implements the engine's `AppPlatform` and drives it. One shared
C++ core (`shared/`), thin per-platform layers.

> This repository contains **no Mojang files**, and nothing in it may ever contain any
> (`tools/tests/no_game_files_test.sh`, run by `make test`). Every build takes your own,
> legally obtained, decrypted copy of the game as input. The Android APK and the iOS app
> **bundle your game files**: they are local builds for your own devices; never commit,
> publish or share them.

| Platform | Build | Output | State |
|---|---|---|---|
| macOS 14+, Apple Silicon | `make app` | `dist/launcher/` (run with `make run`) | Playable: Win10 UI, keyboard + raw mouse look, text entry, sound, resizable window, skins |
| Android 9+ (arm64, 16 KB pages included) | `make android-app IPA=…` | `dist/android/mcfm.apk` | Playable in the emulator (Android 17 and 9): touch, soft keyboard, a mouse and keys, sound, skins; not yet tried on a physical device |
| iPhone / iPad, iOS 15+ | `make ios-app IPA=…` | `dist/ios/mcfm.app` (and `mcfm.ipa`), signed | Reaches the title screen and takes touch on an iPhone 16 Pro Max; hand checks (keys, skins, sound, lifecycle) open |

How it works: [docs/LAUNCHER.md](docs/LAUNCHER.md). State and roadmap: [docs/HANDOFF.md](docs/HANDOFF.md).

## Your game files

All builds start from a **decrypted** Minecraft PE 0.15.10 iOS app (arm64 LC_UUID
`01DFB489-A881-3BDD-8F98-6F016E409625`; the build checks it). Put it in the git-ignored
`game-files/` once and every target finds it:

```sh
make game-files IOS=/path/to/minecraftpe2.ipa   # or the unzipped minecraftpe2.app
```

This writes `game-files/ios/minecraftpe2.ipa` (used by `make android-app` / `ios-app` as `IPA`)
and `game-files/ios/Payload/minecraftpe2.app` (used by `make app` as `GAME`). Without it, pass
`GAME=` / `IPA=` on the command line or set them in `config.mk` (`cp config.example.mk
config.mk`). Details: [docs/GAME_FILES.md](docs/GAME_FILES.md).

## Requirements

- **All:** a Mac on Apple Silicon, Xcode Command Line Tools (`xcode-select --install`), Python 3.
- **macOS launcher:** macOS 14 or newer to play (our loader needs it). `make angle` downloads
  ANGLE once (OpenGL ES on Metal, from a pinned Electron release, ~130 MB).
- **Android app:** `make android-sdk llvm-runtimes android-app-sdk` once (NDK r27d, SDK
  platform 37, build-tools, emulator and system images, LLVM 18 sources, JDK 21, kotlinc;
  about 4 GB, pinned and checksummed, into `~/Library/Android`). No Gradle, no Android Studio.
  The phone: arm64, Android 9 (API 28) or newer.
- **iOS app:** full Xcode at `/Applications/Xcode.app` (used through `DEVELOPER_DIR`), your
  Apple ID in Xcode › Settings › Accounts (a free one works; profiles last 7 days), and an
  iPhone/iPad on iOS 15+ with Developer Mode, connected over USB, paired and unlocked: the build
  signs for it. Once: a provisioning profile for `io.github.d4yvid.mcfm.ios` (see
  [ios/README.md](ios/README.md)). `make angle-ios` builds ANGLE for iOS once (Godot's static
  build, 4 MB download).

## Quick start

### macOS

```sh
make test       # host tests, no game files needed (~1 min)
make angle      # once: ANGLE
make app        # dist/launcher: the converted game image, stubs, ANGLE, mcfm-launch
make check      # loads it and renders 120 frames
make run        # plays it in a window
```

`dist/launcher` reads the game's `data/` from your `GAME` app in place, so keep that app where
it is. `make app`/`check`/`run` refuse while the game runs (`mcfm-launch`). The game runs on our
own Mach-O loader; `LOADER=dyld make run` uses Apple's instead. Turn speed:
`MCFM_LOOK_SCALE=1.5 make run` (raw mouse, no acceleration).

### Android

```sh
make android-sdk llvm-runtimes android-app-sdk   # once
make android-app IPA=/path/to/minecraftpe2.ipa   # dist/android/mcfm.apk
adb install dist/android/mcfm.apk                # on your phone
make android-app-run                             # or: play it in a windowed emulator (API=28: Android 9)
make android-emulator && make android-app-check  # the acceptance check on a headless emulator
```

`IPA` defaults to `game-files/ios/*.ipa`. `make android-app-debug` builds the debuggable
`dist/android/mcfm-debug.apk` that `android-app-run` and `android-app-check` use. More:
[android/README.md](android/README.md).

### iOS

```sh
make angle-ios                                 # once: ANGLE for iOS
make ios-app IPA=/path/to/minecraftpe2.ipa     # dist/ios/mcfm.app, signed for the connected device
make ios-app-run                               # install, launch, show its console here
make ios-app-check                             # on-device check: 300 frames, the title screen
```

The free profile expires after 7 days: `make ios-app` again renews it. More:
[ios/README.md](ios/README.md).

## Controls

- **macOS:** WASD move · mouse look · left click break/attack · right click place/use · Space
  jump · Shift sneak · 1–9 / scroll hotbar · E inventory · Esc pause · T chat. The pointer is
  captured in a world and released in menus and when the window loses focus (Cmd-Tab).
  Ctrl-Cmd-F full screen, Cmd-Q quit.
- **Android:** the game's touch controls; a hardware keyboard (the game's keys) and a mouse
  (buttons, pointer, wheel; no captured mouse look) work too; the soft keyboard for text; back
  is the game's Esc.
- **iOS:** the game's touch controls; hardware keyboard keys; the soft keyboard for text.

Skins: the game's "Choose New Skin" opens a file panel (macOS) or the photo picker (Android, iOS).

## Where worlds live

| Build | Worlds and options |
|---|---|
| macOS launcher | `~/Library/Application Support/MinecraftPE-mcfm/games/com.mojang/` |
| Android app | the app's internal storage, `files/home/games/com.mojang/` (`adb shell run-as io.github.d4yvid.mcfm ls files/home/games/com.mojang`) |
| iOS app | the app's Documents, `games/com.mojang/` (the Files app shows them) |
| Catalyst build (deprecated) | `~/Documents/games/com.mojang/` |

Rebuilding or reinstalling keeps them (`make android-app-run` installs over the existing app).

## Legacy and deprecated builds

Kept working, not developed further:

- **Mac Catalyst app** (deprecated): `make catalyst` → `dist/minecraftpe.app`, `make
  catalyst-run`, `make catalyst-check` (closes a running Catalyst game). The iOS app retagged for
  Mac Catalyst with our dylib injected. See [macos/README.md](macos/README.md).
- **iOS IPA mod** (legacy): `make ios` / `make ios-ipa` → `dist/minecraftpe-mcfm.ipa`
  (unsigned, Win10 UI dylib injected into the original app), `make ios-device` signs and
  installs it. Replaced by the iOS app.
- **Android APK patch** (legacy): `make android` (NDK r10c) / `make android-apk APK=…` →
  `dist/minecraftpe-mcfm.apk` (Mojang's armeabi-v7a APK for Android 6+ with the Win10 UI
  library). Replaced by the Android app.

## Layout

```
shared/    C++11 core, platform-free: loader/ (our Mach-O loader and converter),
           launcher/ (our AppPlatform, engine boot, hooks, input), src/ + include/ (Win10 UI,
           keyboard+mouse, keymap), apple/ (LC_UUID guard, addresses, hook table), tests/
macos/     launcher/ (mcfm-launch: AppKit window, input, audio, loader OS layer),
           tools/ (make_launcher.sh, loader_check), src/ (Catalyst glue, deprecated)
android/   app/ (Kotlin app), launcher/ (libmcfm_launcher.so: render thread, JNI, Darwin
           libSystem layer, Apple-ABI libc++ runtime), jni/ + tools/ (legacy APK patch)
ios/       app/ (Swift app), launcher/ (Objective-C++ glue, ANGLE view), tools/ (build, sign,
           check), src/ (legacy IPA mod)
tools/     launcher/ (image converter, stub generator, ANGLE fetchers), android/ (SDK, emulator,
           app build and checks), ida/ (idalib scripts), loader/, tests/
docs/      HANDOFF, ARCHITECTURE, LAUNCHER, GAME_FILES, research/, superpowers/ (plans, history)
```

Start with [docs/HANDOFF.md](docs/HANDOFF.md) (state, commands, roadmap, pitfalls) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); the launcher plan and its decisions are in
[docs/LAUNCHER.md](docs/LAUNCHER.md), reverse-engineering notes in [docs/research/](docs/research/).

## License

Apache-2.0 (see [LICENSE](LICENSE) and [NOTICE](NOTICE)). Minecraft is a trademark of
Mojang/Microsoft; this project is not affiliated with them. You need your own legally
obtained copy of the game.
