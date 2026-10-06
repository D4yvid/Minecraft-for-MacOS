# Android

Adds the Windows 10 (desktop) UI to Minecraft PE 0.15.10 for Android (armeabi-v7a).
Requires **Android 6.0 or newer**: the mod patches `AppPlatform_android23`, the platform
class the game uses from Android 6; on Android 5 the game uses another class and the mod
has no effect.
Based on the injection code of [runet-client](https://github.com/D4yvid/runet-client).

## Build the library (no game files needed)

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
make android    # build/android/libs/armeabi-v7a/{librunet.so,libgnustl_shared.so}
```

`NDK=` in `config.mk` if it lives elsewhere. Everything in `libminecraftpe.so` is found
with `dlsym` at runtime, so the build does not link against the game.

## Build an APK

Needs `brew install apktool` (pulls in a JDK) and your Minecraft PE 0.15.10 APK.

```sh
make android-apk APK=/path/to/minecraftpe-0.15.10.apk   # dist/minecraftpe-mcfm.apk
adb uninstall com.mojang.minecraftpe   # different signature than the store version
adb install dist/minecraftpe-mcfm.apk
```

`build_apk.sh` decompiles the APK, makes `MainActivity` call
`System.loadLibrary("runet")` right after `gnustl_shared`, adds the two libraries,
rebuilds and signs with a debug key in `build/android/debug.keystore`. The library keeps
the name `librunet.so` and the `runetOnCreate` stub so APKs patched for runet-client work
too.

Logs: `adb logcat -s mcfm`.
