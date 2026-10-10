#!/bin/bash
# Builds the local, git-ignored game-files/ workspace from your own copies of the game.
#
# usage: setup_game_files.sh [--out DIR] [--ios <minecraftpe2.ipa | minecraftpe2.app>]
#                            [--apk <minecraftpe-0.15.10.apk>] [--ida]
#
#   game-files/ios/minecraftpe2.ipa            the IPA (zipped from the .app if you gave an .app)
#   game-files/ios/Payload/minecraftpe2.app    the unzipped app (GAME for make app / make ios-ipa)
#   game-files/ios/minecraftpe2-arm64          thin arm64 binary (what IDA and tools/ida analyse)
#   game-files/ida/minecraftpe2-arm64.i64      IDA database (--ida, ~2 min auto-analysis)
#   game-files/android/apk/<name>.apk          the APK (APK for make android-apk)
#   game-files/android/decompiled/             apktool output (smali, assets, lib/)
#   game-files/android/lib/armeabi-v7a/*.so    the game's native libraries (libminecraftpe.so has symbols)
#
# Every input is checked to be Minecraft PE 0.15.10 before anything is written.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/game-files"; IOS=""; APK=""; IDA=0
die() { echo "setup_game_files: $*" >&2; exit 1; }
abs() { local p="${1%/}"; echo "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --ios) IOS="$(abs "$2")"; shift 2 ;;
    --apk) APK="$(abs "$2")"; shift 2 ;;
    --ida) IDA=1; shift ;;
    *) die "unknown argument $1" ;;
  esac
done
[ -n "$IOS$APK" ] || [ $IDA = 1 ] || die "nothing to do (give --ios and/or --apk)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

if [ -n "$IOS" ]; then
  WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
  case "$IOS" in
    *.ipa) (cd "$WORK" && unzip -q "$IOS") || die "cannot unzip $IOS" ;;
    *.app) mkdir "$WORK/Payload" && cp -R "$IOS" "$WORK/Payload/" ;;
    *) die "--ios needs a .ipa or .app" ;;
  esac
  APPS=("$WORK"/Payload/*.app)
  [ ${#APPS[@]} = 1 ] && [ -d "${APPS[0]}" ] || die "expected one Payload/*.app in $IOS"
  bash "$ROOT/tools/check_game.sh" "${APPS[0]}" >/dev/null || die "unsupported iOS game (see above)"
  EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${APPS[0]}/Info.plist")"
  rm -rf "$OUT/ios" && mkdir -p "$OUT/ios"
  mv "$WORK/Payload" "$OUT/ios/Payload"
  xattr -cr "$OUT/ios/Payload" 2>/dev/null || true
  if [[ "$IOS" == *.ipa ]]; then cp "$IOS" "$OUT/ios/minecraftpe2.ipa"
  else (cd "$OUT/ios" && zip -qry minecraftpe2.ipa Payload); fi
  lipo "$OUT/ios/Payload/$(basename "${APPS[0]}")/$EXE" -thin arm64 -output "$OUT/ios/minecraftpe2-arm64"
  echo "setup_game_files: iOS -> $OUT/ios"
fi

if [ -n "$APK" ]; then
  JDK="$(brew --prefix openjdk 2>/dev/null || true)/bin"; [ -x "$JDK/java" ] && export PATH="$JDK:$PATH"
  command -v apktool >/dev/null || die "apktool not found (brew install apktool)"
  WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
  apktool d -f -r -o "$WORK/decompiled" "$APK" >/dev/null || die "apktool could not decompile $APK"
  bash "$ROOT/android/tools/check_apk_lib.sh" "$WORK/decompiled/lib/armeabi-v7a/libminecraftpe.so" >/dev/null \
    || die "unsupported APK (see above)"
  rm -rf "$OUT/android" && mkdir -p "$OUT/android/apk" "$OUT/android/lib"
  cp "$APK" "$OUT/android/apk/"
  mv "$WORK/decompiled" "$OUT/android/decompiled"
  cp -R "$OUT/android/decompiled/lib/armeabi-v7a" "$OUT/android/lib/"
  echo "setup_game_files: Android -> $OUT/android"
fi

if [ $IDA = 1 ]; then
  BIN="$OUT/ios/minecraftpe2-arm64"
  [ -f "$BIN" ] || die "--ida needs the iOS files (run with --ios first)"
  IDAT="$(ls -d /Applications/IDA*/Contents/MacOS/idat 2>/dev/null | head -1)"
  [ -n "$IDAT" ] || die "IDA not found in /Applications"
  mkdir -p "$OUT/ida"
  cp "$BIN" "$OUT/ida/minecraftpe2-arm64"
  "$IDAT" -A -B -L"$OUT/ida/analysis.log" "$OUT/ida/minecraftpe2-arm64"
  rm -f "$OUT/ida/minecraftpe2-arm64.asm"
  echo "setup_game_files: IDA database -> $OUT/ida/minecraftpe2-arm64.i64"
fi
