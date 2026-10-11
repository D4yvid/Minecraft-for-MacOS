# Local game files (`game-files/`, git-ignored)

The launchers and mods are built from your own copy of Minecraft PE 0.15.10. Everything
derived from the game lives in `game-files/`, which `.gitignore` excludes (and
`tools/tests/no_game_files_test.sh` guards). The Makefile picks it up automatically (override
any of these on the command line or in `config.mk`):

- `GAME` = `ios/Payload/minecraftpe2.app`, the unzipped iOS app (`make app`, `make catalyst`,
  `make ios-ipa`);
- `IPA` = the first `ios/*.ipa` (`make android-app`, `make ios-app`: both apps bundle the game
  from it, so their outputs in `dist/` hold Mojang's files and are never committed);
- `APK` = the first `android/apk/*.apk` that is not `*-patched.apk` (legacy `make android-apk`).

A git worktree without its own `game-files/` uses the main checkout's.

## Layout

| Path | What | Used by |
|---|---|---|
| `ios/minecraftpe2.ipa` | the IPA | `make android-app`, `make ios-app` (`IPA`); `make ios-ipa GAME=…` (either form works) |
| `ios/Payload/minecraftpe2.app` | unzipped, **decrypted** app (fat armv7 + arm64) | `make app` (its `data/` is read in place by `dist/launcher`), `make catalyst`, `make ios-ipa`, tests |
| `ios/minecraftpe2-arm64` | thin arm64 game binary | IDA, `nm`, `otool` |
| `ida/minecraftpe2-arm64.i64` | IDA database (auto-analysed) + its input binary | `tools/ida/*.py` via `.venv` |
| `android/apk/*.apk` | the Android APK(s) (only for the legacy mod) | `make android-apk`, `android/tests/apk_test.sh` |
| `android/decompiled/` | `apktool d -r` output (smali, assets, `lib/`) | reading smali, patch experiments |
| `android/lib/armeabi-v7a/` | `libminecraftpe.so` (**with symbols** — names everything), `libfmod.so`, `libgnustl_shared.so` | `nm -D`, `readelf`, naming iOS functions |

## Setting it up

```bash
make game-files IOS=/path/to/minecraftpe2.ipa            # or the unzipped .app
make game-files APK_IN=/path/to/minecraftpe-0.15.10.apk  # Android APK (legacy mod, symbols)
make game-files IOS=/path/to/minecraftpe2.app IDA=1      # also build the IDA database (~2 min)
```

Each input is checked first (`tools/check_game.sh`: arm64 UUID
`01DFB489-A881-3BDD-8F98-6F016E409625`, decrypted; `android/tools/check_apk_lib.sh`:
0.15.10 symbols). The idalib environment is `.venv/` (see `tools/ida/README.md`).

## Provenance on the owner's machine

- iOS: from `~/Downloads/Payload/minecraftpe2.app` (the decrypted app extracted from the
  owner's IPA). The original `.ipa` file is not on the machine, so
  `ios/minecraftpe2.ipa` was rebuilt by zipping `Payload/` (same contents as an IPA).
- Android: **no original APK yet** (the Android app needs none: it is built from the IPA). `android/apk/minecraftpe-0.15.10-standin.apk` and the
  `decompiled/` tree come from an earlier injection project's decompiled APK (so the smali
  has that project's edits); `…-standin-*-patched.apk` also loads its library. The `.so` files
  are the game's own 0.15.10 libraries. Replace with an original APK when available:
  `make game-files APK_IN=…`.
