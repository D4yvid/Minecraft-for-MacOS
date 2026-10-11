#!/bin/bash
# fetch_angle_ios.sh: links ANGLE's static iOS libraries (Godot's build) into one libGLESv2.dylib
# only when the zip's SHA-256 matches, and leaves nothing behind otherwise. Stand-in archives,
# no network; skipped without the iPhoneOS SDK.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || { echo "fetch_angle_ios_test: skipped (no iPhoneOS SDK)"; exit 0; }
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T/zip"
CC=(xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -c)
echo "void glDrawArrays(unsigned m, int f, int c) {}" > "$T/gles.c"
echo "void *eglGetDisplay(void *d) { return d; }" > "$T/egl.c"
echo "int angle_internal(void) { return 7; }" > "$T/angle.c"
for n in gles egl angle; do "${CC[@]}" "$T/$n.c" -o "$T/$n.o"; done
ar rcs "$T/zip/libGLES.ios.arm64.a" "$T/gles.o"; ar rcs "$T/zip/libEGL.ios.arm64.a" "$T/egl.o"
ar rcs "$T/zip/libANGLE.ios.arm64.a" "$T/angle.o"
(cd "$T/zip" && zip -qr "$T/angle.zip" .)
SUM="$(shasum -a 256 "$T/angle.zip" | awk '{print $1}')"
URL="file://${T// /%20}/angle.zip"
fails=0
ANGLE_IOS_URL="$URL" ANGLE_IOS_SHA256="$SUM" bash "$ROOT/tools/launcher/fetch_angle_ios.sh" "$T/out" >/dev/null \
  || { echo "FAIL: good archive refused"; fails=$((fails+1)); }
D="$T/out/libGLESv2.dylib"
[ -f "$D" ] || { echo "FAIL: libGLESv2.dylib missing"; fails=$((fails+1)); }
[ "$(otool -D "$D" 2>/dev/null | tail -1)" = "@rpath/libGLESv2.dylib" ] || { echo "FAIL: install name not @rpath"; fails=$((fails+1)); }
vtool -show-build "$D" 2>/dev/null | grep -q 'platform IOS$' || { echo "FAIL: not an iOS dylib"; fails=$((fails+1)); }
nm -gU "$D" | grep -q " _glDrawArrays$" && nm -gU "$D" | grep -q " _eglGetDisplay$" || { echo "FAIL: GL/EGL entry points not exported"; fails=$((fails+1)); }
ANGLE_IOS_URL="$URL" ANGLE_IOS_SHA256="0000" bash "$ROOT/tools/launcher/fetch_angle_ios.sh" "$T/bad" >/dev/null 2>&1 \
  && { echo "FAIL: wrong checksum accepted"; fails=$((fails+1)); }
ls "$T/bad"/*.dylib >/dev/null 2>&1 && { echo "FAIL: files left after checksum failure"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "fetch_angle_ios_test: passed" || { echo "$fails failure(s)"; exit 1; }
