# Local game files (`game-files/`, git-ignored)

The mods are built from your own copy of Minecraft PE 0.15.10. Everything derived from the
game lives in `game-files/`, which `.gitignore` excludes (and `tools/tests/no_game_files_test.sh`
guards). The Makefile picks it up automatically: `GAME` = the unzipped iOS app, `APK` = the
Android APK (override either in `config.mk`).

## Layout

| Path | What | Used by |
|---|---|---|
| `ios/minecraftpe2.ipa` | the IPA | `make ios-ipa GAME=…` (either form works) |
| `ios/Payload/minecraftpe2.app` | unzipped, **decrypted** app (fat armv7 + arm64) | `make app`, `make ios-ipa`, tests |
| `ios/minecraftpe2-arm64` | thin arm64 game binary | IDA, `nm`, `otool` |
| `ida/minecraftpe2-arm64.i64` | IDA database (auto-analysed) + its input binary | `tools/ida/*.py` via `.venv` |
| `android/apk/*.apk` | the Android APK(s) | `make android-apk`, `android/tests/apk_test.sh` |
| `android/decompiled/` | `apktool d -r` output (smali, assets, `lib/`) | reading smali, patch experiments |
| `android/lib/armeabi-v7a/` | `libminecraftpe.so` (**with symbols** — names everything), `libfmod.so`, `libgnustl_shared.so` | `nm -D`, `readelf`, naming iOS functions |

## Setting it up

```bash
make game-files IOS=/path/to/minecraftpe2.ipa            # or the unzipped .app
make game-files APK_IN=/path/to/minecraftpe-0.15.10.apk  # Android
make game-files IOS=/path/to/minecraftpe2.app IDA=1      # also build the IDA database (~2 min)
```

Each input is checked first (`tools/check_game.sh`: arm64 UUID
`01DFB489-A881-3BDD-8F98-6F016E409625`, decrypted; `android/tools/check_apk_lib.sh`:
0.15.10 symbols). The idalib environment is `.venv/` (see `tools/ida/README.md`).

## Provenance on the owner's machine

- iOS: from `~/Downloads/Payload/minecraftpe2.app` (the decrypted app extracted from the
  owner's IPA). The original `.ipa` file is not on the machine, so
  `ios/minecraftpe2.ipa` was rebuilt by zipping `Payload/` (same contents as an IPA).
- Android: **no original APK yet.** `android/apk/minecraftpe-0.15.10-standin.apk` and the
  `decompiled/` tree come from runet-client's decompiled APK (so the smali has runet's
  `runetOnCreate` edits); `…-standin-runet-patched.apk` also loads `librunet`. The `.so` files
  are the game's own 0.15.10 libraries. Replace with an original APK when available:
  `make game-files APK_IN=…`.
