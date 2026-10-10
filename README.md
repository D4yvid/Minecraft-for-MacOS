# Minecraft for macOS (and iOS, Android)

Mods for **Minecraft: Pocket Edition 0.15.10** that run it natively on Apple Silicon Macs
with the Windows 10 (desktop) UI, keyboard + mouse and a resizable window, and bring
the same desktop UI to iOS and Android. One shared C++ core, three thin platform layers.

> This repository contains **no Mojang files**. Every build takes your own copy of the
> game as input (a decrypted iOS `.app`/`.ipa`, or an Android `.apk`).

| Platform | Output | Features |
|---|---|---|
| macOS (Apple Silicon) | `dist/minecraftpe.app` | Win10 UI, keyboard + mouse (pointer capture), resizable window, auto-hiding title bar, no App Store sign-in prompt |
| iOS | `dist/minecraftpe-mcfm.ipa` (unsigned) | Win10 UI, no App Store sign-in prompt |
| Android 6+ (armeabi-v7a) | `dist/minecraftpe-mcfm.apk` (debug-signed) | Win10 UI |

## Requirements

- macOS on Apple Silicon, Xcode Command Line Tools (`xcode-select --install`), Python 3
- **macOS / iOS:** a *decrypted* Minecraft PE 0.15.10 iOS app (arm64 UUID
  `01DFB489-A881-3BDD-8F98-6F016E409625`; the build checks this)
- **iOS dylib:** full Xcode (for the iPhoneOS SDK)
- **Android:** Android NDK r10c (runs under Rosetta), `brew install apktool`, and your
  Minecraft PE 0.15.10 APK

## Quick start (macOS)

```sh
cp config.example.mk config.mk     # then set GAME = /path/to/minecraftpe2.app
make test                          # host tests, no game files needed
make app                           # builds dist/minecraftpe.app from your copy
make check                         # verifies the bundle and launches it for 15 s
make run
```

Controls: WASD move · mouse look · left click break/attack · right click place/use ·
Space jump · Shift sneak · 1–9 / scroll hotbar · E inventory · Esc pause · T chat.
Hover the top-left corner to reveal the title bar.

Your worlds live in `~/Documents/games/com.mojang`, outside the app, so rebuilding keeps
them. `make app` refuses to run while the game is open.

## Other platforms

- iOS: `make ios` (needs Xcode), then `make ios-ipa` → sign and sideload. See [ios/README.md](ios/README.md).
- Android: `make android`, then `make android-apk APK=…`. See [android/README.md](android/README.md).

## Layout

```
shared/   C++11 core used by every platform (Platform interface, Win10 UI, keyboard+mouse,
          input logic) + shared/apple (address table, LC_UUID guard) for iOS and macOS
macos/    Mac Catalyst glue + app conversion        ios/      iOS dylib + IPA packaging
android/  JNI / injection + APK packaging            tools/    injector, game check, IDA scripts
docs/     ARCHITECTURE.md and design history
```

Start with [docs/HANDOFF.md](docs/HANDOFF.md) (state, roadmap) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); reverse-engineering notes are in [docs/research/](docs/research/).

## License

Apache-2.0 (see [LICENSE](LICENSE) and [NOTICE](NOTICE)). Minecraft is a trademark of
Mojang/Microsoft; this project is not affiliated with them. You need your own legally
obtained copy of the game.
