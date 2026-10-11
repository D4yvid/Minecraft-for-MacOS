# Android

## The app (`app/`, `launcher/`)

Our own APK, "Minecraft PE (mcfm)" (`io.github.d4yvid.mcfm`): it runs the **iOS** version
0.15.10 of Minecraft PE with our Mach-O loader (docs/LAUNCHER.md, Stage 3). arm64, Android 9
(API 28) or newer, 16 KB-page devices included. The APK is a launcher for the game bundled in
it: the build takes your decrypted `minecraftpe` IPA and puts its game binary and `data/` in the
APK, and the first start prepares them (the binary is converted on the device). The APK holds
Mojang's files: it is a local build, never committed. Not on Google Play; install it directly.

```sh
make android-sdk llvm-runtimes android-app-sdk   # once: NDK r27d, SDK, LLVM sources, JDK, kotlinc
make android-app IPA=/path/to/minecraftpe.ipa    # dist/android/mcfm.apk (android-app-debug: debuggable)
adb install dist/android/mcfm.apk
make android-app-run IPA=…                       # play it in the emulator, in a window (API=28: Android 9)
make android-emulator && make android-app-check IPA=…   # the acceptance check
```

- `app/`: the Kotlin app (`GameActivity`: the launcher, the game's surface, lifecycle, touch,
  keys, mouse, soft keyboard and skin picker; `GameFiles`: preparing the bundled game), the
  manifest and resources. The IPA is found in `game-files/ios/` when `IPA=` is not given. Built without Gradle by `tools/android/build_app.sh` (aapt2, kotlinc, d8,
  zipalign -P 16, apksigner with the local debug key `build/android/debug.keystore`).
- `launcher/`: `libmcfm_launcher.so` (the loader, the Darwin libSystem layer `darwin/`, the
  Apple-ABI libc++ `runtime/`, the render thread `game_thread.cpp`, the JNI entry points
  `app.cpp`) and the stubs of the game's Apple frameworks (`libmcfm_stub_*.so`, from
  `game_imports.tsv`).
- Worlds and options: the app's internal storage (`adb shell run-as io.github.d4yvid.mcfm ls
  files/home/games/com.mojang`). Logs: `adb logcat -s mcfm`.

## Legacy: the Win10 UI mod for Mojang's APK (the rest of `android/`)

Adds the Windows 10 (desktop) UI to Minecraft PE 0.15.10 for Android (armeabi-v7a).
Requires **Android 6.0 or newer**: the mod patches `AppPlatform_android23`, the platform
class the game uses from Android 6; on Android 5 the game uses another class and the mod
has no effect.
It patches Mojang's Android APK. The app above replaces it; the mod is kept as it is.

### Build the library (no game files needed)

Needs Android NDK r10c. On Apple Silicon it runs under Rosetta; the `.bin` installer is
32-bit, so unpack it with 7-Zip instead of running it:

```sh
mkdir -p ~/Library/Android/ndk && cd ~/Library/Android/ndk
curl -LO https://dl.google.com/android/ndk/android-ndk-r10c-darwin-x86_64.bin
7zz x -snld android-ndk-r10c-darwin-x86_64.bin   # brew install sevenzip
```

(7-Zip may still skip a few links that point at other links, leaving empty files, e.g.
`toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/arm-linux-androideabi/bin/ld`;
replace each with the link 7-Zip names in its error output.)

```sh
make android    # build/android/libs/armeabi-v7a/{libmcfm.so,libgnustl_shared.so}
```

`NDK=` in `config.mk` if it lives elsewhere. Everything in `libminecraftpe.so` is found
with `dlsym` at runtime, so the build does not link against the game.

### Build an APK

Needs `brew install apktool` (pulls in a JDK) and your Minecraft PE 0.15.10 APK.

```sh
make android-apk APK=/path/to/minecraftpe-0.15.10.apk   # dist/minecraftpe-mcfm.apk
adb uninstall com.mojang.minecraftpe   # different signature than the store version
adb install dist/minecraftpe-mcfm.apk
```

`build_apk.sh` decompiles the APK, makes `MainActivity` call
`System.loadLibrary("mcfm")` right after `gnustl_shared`, adds the two libraries,
rebuilds and signs with a debug key in `build/android/debug.keystore`.

Logs: `adb logcat -s mcfm`.
