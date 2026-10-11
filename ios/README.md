# iOS

## The app (`app/`, `launcher/`)

Our own Swift app, "Minecraft PE (mcfm)" (`io.github.d4yvid.mcfm.ios`), a launcher for the iOS
game bundled in it (docs/LAUNCHER.md, Stage 4): the game binary converted at build time and
loaded by dyld, our AppPlatform (`shared/launcher`), ANGLE on Metal. iPhone/iPad, iOS 15+,
landscape, full screen. The app holds Mojang's files: a local build, never committed.

```sh
make angle-ios                         # once: ANGLE for iOS (Godot's static build)
make ios-app IPA=/path/to/minecraftpe.ipa   # dist/ios/mcfm.app (and .ipa), signed
make ios-app-run                       # install on the connected iPhone, launch, show the console
make ios-app-check                     # the device check (keep the phone unlocked, untouched)
```

Needs Xcode (used through `DEVELOPER_DIR`, no `xcode-select` change), your Apple ID in Xcode ›
Settings › Accounts, and once a provisioning profile for the bundle id: open any project with
bundle id `io.github.d4yvid.mcfm.ios` and choose your team under Signing & Capabilities. Free
profiles last 7 days (`make ios-app` again renews). On the device: Developer Mode, and trust the
developer once (Settings › General › VPN & Device Management). Worlds are in the app's
Documents (the Files app shows them).

- `app/`: `AppDelegate`/`SceneDelegate`, `GameViewController` (display link, lifecycle, photo
  picker, splash), `GameView` (Metal layer, touches, keys, soft keyboard), `Info.plist`, launch
  screen.
- `launcher/`: `mcfm_ios.h` (the C API Swift calls), `launcher.mm` (dyld, hooks, the engine),
  `egl_view.mm` (ANGLE), `ios_keymap`, `layout.h`.
- `tools/`: `build_app.sh`, `signing.sh`, `app_check.sh`, `print_hooks.cpp`.

## Legacy: the mod injected into the IPA

Adds the Windows 10 (desktop) UI to Minecraft PE 0.15.10 on iPhone / iPad and skips the
App Store sign-in prompt. Touch controls are unchanged.

### Build

Needs full Xcode (the Command Line Tools have no iPhoneOS SDK) and a *decrypted*
0.15.10 `.ipa` or `.app` (`GAME=` in `config.mk`).

```sh
make ios        # build/ios/libmcfm.dylib (arm64, iOS 15+)
make ios-ipa    # dist/minecraftpe-mcfm.ipa: arm64-only, dylib injected, UNSIGNED
```

`make ios-device` signs it with your Apple Development identity and installs it on the
connected device (`tools/sign_install.sh`). Or sign and install the IPA with your own tooling (e.g. a sideloading app with your Apple ID,
or TrollStore); it must re-sign the app and `Frameworks/libmcfm.dylib`.

Without Xcode, `make test` still compiles the iOS sources for the iOS target
(`make ios-syntax`), and `ios/tests/ipa_test.sh` checks the IPA packaging with a stand-in
dylib.

Logs: `mcfm: …` lines in the device console.
