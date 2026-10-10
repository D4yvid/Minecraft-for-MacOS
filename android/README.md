# Android

## The app (`app/`, `launcher/`)

Our own APK, "Minecraft PE (mcfm)" (`io.github.d4yvid.mcfm`): it runs the **iOS** version
0.15.10 of Minecraft PE with our Mach-O loader (docs/LAUNCHER.md, Stage 3). arm64, Android 9
(API 28) or newer, 16 KB-page devices included. Nothing from Mojang is in the APK: on first
start you choose your own decrypted `minecraftpe` IPA and the app imports it (the game binary,
converted on the device, and its `data/`). Not on Google Play (its policy forbids running code
that did not come from Play); install the APK directly.

```sh
make android-sdk llvm-runtimes android-app-sdk   # once: NDK r27d, SDK, LLVM sources, JDK, kotlinc
make android-app                                 # dist/android/mcfm.apk
adb install dist/android/mcfm.apk
make android-emulator && make android-app-check IPA=/path/to/minecraftpe.ipa   # the acceptance check
```

- `app/`: the Kotlin activities (`ImportActivity`: the IPA picker and import; `GameActivity`:
  the game's surface, lifecycle, touch, keys, mouse and soft keyboard), the manifest and
  resources. Built without Gradle by `tools/android/build_app.sh` (aapt2, kotlinc, d8,
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
