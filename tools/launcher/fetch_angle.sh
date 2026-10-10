#!/bin/bash
# usage: fetch_angle.sh <outdir>
# ANGLE (OpenGL ES on Metal) for the launcher: libEGL.dylib and libGLESv2.dylib from an
# official Electron macOS arm64 release, verified by SHA-256. Nothing is committed.
# Pinned to v41.0.3: later releases (v44) link ANGLE into the framework and no longer ship
# the two dylibs.
set -euo pipefail
OUT="$1"
ANGLE_URL="${ANGLE_URL:-https://github.com/electron/electron/releases/download/v41.0.3/electron-v41.0.3-darwin-arm64.zip}"
ANGLE_SHA256="${ANGLE_SHA256:-d8aeeb263b234a243411e02b3828e9349fe976417798334cba9f34f6eae28d7e}"
LIBS="Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/electron.zip" "$ANGLE_URL"
GOT="$(shasum -a 256 "$TMP/electron.zip" | awk '{print $1}')"
[ "$GOT" = "$ANGLE_SHA256" ] || { echo "fetch_angle: checksum mismatch ($GOT, expected $ANGLE_SHA256)" >&2; exit 1; }
unzip -q "$TMP/electron.zip" "$LIBS/libEGL.dylib" "$LIBS/libGLESv2.dylib" -d "$TMP/x"
for l in libEGL libGLESv2; do
  lipo -archs "$TMP/x/$LIBS/$l.dylib" | tr ' ' '\n' | grep -qx arm64 || { echo "fetch_angle: $l has no arm64" >&2; exit 1; }
done
mkdir -p "$OUT"
# Electron names them "./libX.dylib" (relative to the current directory): use @rpath, so they
# load next to mcfm-launch and the stubs, then re-sign (ad hoc) after the edit.
for l in libEGL libGLESv2; do
  f="$TMP/x/$LIBS/$l.dylib"
  install_name_tool -id "@rpath/$l.dylib" "$f" 2>/dev/null
  otool -L "$f" | awk 'NR>1 && $1 ~ /^\.\// {print $1}' | while read -r dep; do
    install_name_tool -change "$dep" "@rpath/${dep#./}" "$f" 2>/dev/null
  done
  codesign -f -s - "$f" 2>/dev/null
done
cp "$TMP/x/$LIBS/libEGL.dylib" "$TMP/x/$LIBS/libGLESv2.dylib" "$OUT/"
echo "fetch_angle: $OUT ready ($(basename "$ANGLE_URL"))"
