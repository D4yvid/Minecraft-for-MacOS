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

`IPA` defaults to `game-files/ios/*.ipa` (`make game-files`). Needs Xcode at
`/Applications/Xcode.app` (used through `DEVELOPER_DIR`, no `xcode-select` change), your Apple
ID in Xcode › Settings › Accounts, and once a provisioning profile for the bundle id: open any
project with bundle id `io.github.d4yvid.mcfm.ios` and choose your team under Signing &
Capabilities. The device must be connected and paired when you build: the build picks the
profile that includes it. Free profiles last 7 days (`make ios-app` again renews). On the
device: Developer Mode, and trust the developer once (Settings › General › VPN & Device
Management). Worlds and options are in the app's Documents, `games/com.mojang/` (the Files app
shows them). Controls: the game's touch controls, hardware keyboard keys, the soft keyboard for
text. Logs: `mcfm:` lines in the `make ios-app-run` console.

- `app/`: `AppDelegate`/`SceneDelegate`, `GameViewController` (display link, lifecycle, photo
  picker, splash), `GameView` (Metal layer, touches, keys, soft keyboard), `SplashView`,
  `Bridging.h`, `Info.plist`, launch screen.
- `launcher/`: `mcfm_ios.h` (the C API Swift calls), `launcher.mm` (dyld, hooks, the engine),
  `egl_view.mm` (ANGLE), `ios_keymap`, `layout.h`.
- `tools/`: `build_app.sh`, `signing.sh`, `app_check.sh`, `print_hooks.cpp`.

## Legacy: the mod injected into the IPA (`src/`, `tools/make_ipa.sh`, `tools/sign_install.sh`)

Replaced by the app above; kept as it is. Adds the Windows 10 (desktop) UI to Minecraft PE 0.15.10 on iPhone / iPad and skips the
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

Without Xcode, `make test` still compiles the mod's sources for the iOS target
(`make ios-syntax`); `ios/tests/ipa_test.sh` (run by `make catalyst-check`) checks the IPA
packaging with a stand-in dylib. For the app, `make test` runs `ios_keymap_test`,
`layout_test`, `signing_test.sh` and `fetch_angle_ios_test.sh` on the host.

Logs: `mcfm: …` lines in the device console.
