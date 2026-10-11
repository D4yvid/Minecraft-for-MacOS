#!/bin/bash
# usage: fetch_angle_ios.sh <outdir>   (make angle-ios)
# ANGLE (OpenGL ES on Metal) for the iOS app (docs/LAUNCHER.md, Stage 4): Godot's static iOS
# arm64 build (godotengine/godot-angle-static, verified by SHA-256) linked into one
# <outdir>/libGLESv2.dylib with both the EGL and the GLES entry points, plus the few functions
# Godot supplies itself (tools/launcher/angle_ios_shims.cpp). Nothing is committed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"
ANGLE_IOS_URL="${ANGLE_IOS_URL:-https://github.com/godotengine/godot-angle-static/releases/download/chromium%2F7578/godot-angle-static-arm64-ios-release.zip}"
ANGLE_IOS_SHA256="${ANGLE_IOS_SHA256:-ee57e8e6f6d5477abda3d3956d53e75f04357ddeec95fdfd877b03e000ab548f}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
run() { local out; out="$("$@" 2>&1)" || { echo "fetch_angle_ios: $1 failed: $out" >&2; exit 1; }; }
curl -fsSL -o "$TMP/angle.zip" "$ANGLE_IOS_URL"
GOT="$(shasum -a 256 "$TMP/angle.zip" | awk '{print $1}')"
[ "$GOT" = "$ANGLE_IOS_SHA256" ] || { echo "fetch_angle_ios: checksum mismatch ($GOT, expected $ANGLE_IOS_SHA256)" >&2; exit 1; }
unzip -q "$TMP/angle.zip" libGLES.ios.arm64.a libEGL.ios.arm64.a libANGLE.ios.arm64.a -d "$TMP/x" \
  || { echo "fetch_angle_ios: the archive has no lib{GLES,EGL,ANGLE}.ios.arm64.a" >&2; exit 1; }
run xcrun --sdk iphoneos clang++ -target arm64-apple-ios15.0 -std=c++17 -O2 -dynamiclib \
  -install_name @rpath/libGLESv2.dylib "$ROOT/tools/launcher/angle_ios_shims.cpp" \
  -Wl,-all_load "$TMP/x/libGLES.ios.arm64.a" "$TMP/x/libEGL.ios.arm64.a" "$TMP/x/libANGLE.ios.arm64.a" \
  -framework Foundation -framework Metal -framework QuartzCore -framework IOSurface -framework CoreGraphics \
  -framework UIKit -o "$TMP/libGLESv2.dylib"
mkdir -p "$OUT"
cp "$TMP/libGLESv2.dylib" "$OUT/"
echo "fetch_angle_ios: $OUT/libGLESv2.dylib ready ($(basename "$ANGLE_IOS_URL"))"
