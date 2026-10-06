#!/bin/bash
# usage: apk_test.sh <Minecraft PE 0.15.10 .apk> <built librunet.so dir>
# Builds a modded APK and checks its contents and signature.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APK="${1:?usage: apk_test.sh <apk> <libs dir>}"; LIBS="${2:?libs dir}"
JDK="$(brew --prefix openjdk 2>/dev/null)/bin"; [ -x "$JDK/java" ] && export PATH="$JDK:$PATH"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
MCFM_KEYSTORE="$T/debug.keystore" bash "$ROOT/android/tools/build_apk.sh" "$APK" "$LIBS" "$T/out.apk" >"$T/log" 2>&1 || { cat "$T/log"; fail "build_apk.sh failed"; }
[ -f "$T/out.apk" ] || { echo "no APK produced"; exit 1; }
LIST="$(unzip -l "$T/out.apk")"
for lib in librunet.so libgnustl_shared.so libminecraftpe.so; do
  grep -q "lib/armeabi-v7a/$lib" <<<"$LIST" || fail "$lib missing from the APK"
done
unzip -q -o "$T/out.apk" lib/armeabi-v7a/librunet.so -d "$T/x"
cmp -s "$T/x/lib/armeabi-v7a/librunet.so" "$LIBS/librunet.so" || fail "APK has a different librunet.so than the build"
jarsigner -verify "$T/out.apk" >/dev/null 2>&1 || fail "APK signature does not verify"
apktool d -f -r -o "$T/d" "$T/out.apk" >/dev/null 2>&1 || fail "cannot decompile the result"
n="$(grep -c 'const-string v[0-9]*, "runet"' "$T/d/smali/com/mojang/minecraftpe/MainActivity.smali" 2>/dev/null)"
[ "$n" = 1 ] || fail "MainActivity loads runet $n times (want 1)"
[ $fails = 0 ] && echo "apk_test: passed" || { echo "$fails failure(s)"; exit 1; }
