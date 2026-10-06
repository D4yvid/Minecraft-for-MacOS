# iOS

Adds the Windows 10 (desktop) UI to Minecraft PE 0.15.10 on iPhone / iPad and skips the
App Store sign-in prompt. Touch controls are unchanged.

## Build

Needs full Xcode (the Command Line Tools have no iPhoneOS SDK) and a *decrypted*
0.15.10 `.ipa` or `.app` (`GAME=` in `config.mk`).

```sh
make ios        # build/ios/libmcfm.dylib (arm64, iOS 15+)
make ios-ipa    # dist/minecraftpe-mcfm.ipa: arm64-only, dylib injected, UNSIGNED
```

Sign and install the IPA with your own tooling (e.g. a sideloading app with your Apple ID,
or TrollStore); it must re-sign the app and `Frameworks/libmcfm.dylib`.

Without Xcode, `make test` still compiles the iOS sources for the iOS target
(`make ios-syntax`), and `ios/tests/ipa_test.sh` checks the IPA packaging with a stand-in
dylib.

Logs: `mcfm: …` lines in the device console.
