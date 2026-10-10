#!/bin/bash
# fetch_angle.sh: extracts the two ANGLE dylibs from a release zip only when the SHA-256
# matches; otherwise leaves nothing behind. Uses a local stand-in zip (no network).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
LIBS="Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries"
mkdir -p "$T/zip/$LIBS"
# Like Electron's: install names "./libX.dylib", libEGL linked against "./libGLESv2.dylib".
echo "int libGLESv2(void){return 0;}" > "$T/libGLESv2.c"
clang -arch arm64 -dynamiclib "$T/libGLESv2.c" -install_name ./libGLESv2.dylib -o "$T/zip/$LIBS/libGLESv2.dylib"
echo "int libGLESv2(void); int libEGL(void){return libGLESv2();}" > "$T/libEGL.c"
clang -arch arm64 -dynamiclib "$T/libEGL.c" "$T/zip/$LIBS/libGLESv2.dylib" -install_name ./libEGL.dylib -o "$T/zip/$LIBS/libEGL.dylib"
(cd "$T/zip" && zip -qry "$T/electron.zip" Electron.app)
SUM="$(shasum -a 256 "$T/electron.zip" | awk '{print $1}')"
URL="file://${T// /%20}/electron.zip"  # URLs carry no raw spaces
fails=0
ANGLE_URL="$URL" ANGLE_SHA256="$SUM" bash "$ROOT/tools/launcher/fetch_angle.sh" "$T/out" >/dev/null \
  || { echo "FAIL: good archive refused"; fails=$((fails+1)); }
for l in libEGL libGLESv2; do [ -f "$T/out/$l.dylib" ] || { echo "FAIL: $l.dylib missing"; fails=$((fails+1)); }; done
for l in libEGL libGLESv2; do
  [ "$(otool -D "$T/out/$l.dylib" 2>/dev/null | tail -1)" = "@rpath/$l.dylib" ] || { echo "FAIL: $l install name not @rpath"; fails=$((fails+1)); }
done
otool -L "$T/out/libEGL.dylib" | grep -q "^[[:space:]]*\./" && { echo "FAIL: libEGL still depends on ./ paths"; fails=$((fails+1)); }
codesign -v "$T/out/libEGL.dylib" 2>/dev/null || { echo "FAIL: libEGL not validly signed after rewrite"; fails=$((fails+1)); }
ANGLE_URL="$URL" ANGLE_SHA256="0000" bash "$ROOT/tools/launcher/fetch_angle.sh" "$T/bad" >/dev/null 2>&1 \
  && { echo "FAIL: wrong checksum accepted"; fails=$((fails+1)); }
ls "$T/bad"/*.dylib >/dev/null 2>&1 && { echo "FAIL: files left after checksum failure"; fails=$((fails+1)); }
# An archive without the two libraries: refused with a message, nothing left behind.
mkdir -p "$T/empty/Electron.app"; echo x > "$T/empty/Electron.app/README"
(cd "$T/empty" && zip -qry "$T/empty.zip" Electron.app)
ESUM="$(shasum -a 256 "$T/empty.zip" | awk '{print $1}')"
ANGLE_URL="file://${T// /%20}/empty.zip" ANGLE_SHA256="$ESUM" bash "$ROOT/tools/launcher/fetch_angle.sh" "$T/none" 2>"$T/err" >/dev/null \
  && { echo "FAIL: archive without libraries accepted"; fails=$((fails+1)); }
grep -q "has no" "$T/err" || { echo "FAIL: no message for missing libraries: $(cat "$T/err")"; fails=$((fails+1)); }
[ -e "$T/none" ] && { echo "FAIL: output directory created for a bad archive"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "fetch_angle_test: passed" || { echo "$fails failure(s)"; exit 1; }
