#!/bin/bash
# usage: build_apk.sh <Minecraft PE 0.15.10 .apk> <libs dir with librunet.so + libgnustl_shared.so> <out.apk>
# Decompiles your APK, makes MainActivity load librunet, adds the libraries, rebuilds and
# signs it with a local debug key (MCFM_KEYSTORE, default build/android/debug.keystore).
# Needs apktool and a JDK (brew install apktool).
set -euo pipefail
APK="$1"; LIBS="$2"; OUT="$3"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"  # repo root
die() { echo "build_apk: $*" >&2; exit 1; }
JDK="$(brew --prefix openjdk 2>/dev/null || true)/bin"; [ -x "$JDK/java" ] && export PATH="$JDK:$PATH"
command -v apktool >/dev/null || die "apktool not found (brew install apktool)"
command -v jarsigner >/dev/null && command -v keytool >/dev/null || die "a JDK is needed (brew install openjdk)"
[ -f "$APK" ] || die "APK $APK not found"
for lib in librunet.so libgnustl_shared.so; do [ -f "$LIBS/$lib" ] || die "$LIBS/$lib missing (run make android)"; done
case "$OUT" in *.apk) ;; *) die "output must end in .apk: $OUT" ;; esac
KEYSTORE="${MCFM_KEYSTORE:-$ROOT/build/android/debug.keystore}"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
# -r: keep resources compiled; only smali and lib/ change.
apktool d -f -r -o "$WORK/app" "$APK" >/dev/null || die "apktool could not decompile $APK"
MAIN="$WORK/app/smali/com/mojang/minecraftpe/MainActivity.smali"
[ -f "$MAIN" ] || die "$APK is not Minecraft PE (no MainActivity)"
[ -f "$WORK/app/lib/armeabi-v7a/libminecraftpe.so" ] || die "$APK has no armeabi-v7a libminecraftpe.so"
bash "$ROOT/android/tools/check_apk_lib.sh" "$WORK/app/lib/armeabi-v7a/libminecraftpe.so" >/dev/null || die "unsupported APK"
python3 -I "$ROOT/android/tools/patch_smali.py" "$MAIN" >/dev/null
cp "$LIBS/librunet.so" "$LIBS/libgnustl_shared.so" "$WORK/app/lib/armeabi-v7a/"
apktool b -o "$WORK/unsigned.apk" "$WORK/app" >/dev/null || die "apktool could not rebuild the APK"

if [ ! -f "$KEYSTORE" ]; then
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -keystore "$KEYSTORE" -alias mcfm -storepass android -keypass android \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=mcfm debug" >/dev/null 2>&1 || die "keytool failed"
fi
jarsigner -keystore "$KEYSTORE" -storepass android -keypass android -sigalg SHA256withRSA -digestalg SHA-256 \
  "$WORK/unsigned.apk" mcfm >/dev/null || die "jarsigner failed"
mkdir -p "$(dirname "$OUT")"
mv "$WORK/unsigned.apk" "$OUT"
echo "build_apk: wrote $OUT (debug-signed; uninstall the store version before installing)"
