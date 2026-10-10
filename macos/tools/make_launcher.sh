#!/bin/bash
# usage: make_launcher.sh <minecraftpe2.app> <outdir> <mcfm-launch> <angle-dir>
# Builds the Mach-O launcher directory from your decrypted game: the arm64 executable
# converted to libminecraftpe.dylib, stubs for every non-host library it imports, and
# mcfm-launch. Nothing here is committed (dist/ is git-ignored).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$1"; OUT="$2"; BIN="$3"; ANGLE="$4"
bash "$ROOT/tools/check_game.sh" "$APP"
EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist")"
rm -rf "$OUT"; mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
lipo -thin arm64 "$APP/$EXE" -output "$TMP/game"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$TMP/game" > "$OUT/imports.tsv"
cp "$ANGLE/libEGL.dylib" "$ANGLE/libGLESv2.dylib" "$OUT/"
bash "$ROOT/tools/launcher/build_stubs.sh" "$OUT/imports.tsv" "$OUT" --provider "OpenGLES=$OUT/libGLESv2.dylib"
rm -rf "$OUT/src"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$TMP/game" "$OUT/libminecraftpe.dylib"
cp "$BIN" "$OUT/mcfm-launch"
for f in "$OUT"/*.dylib "$OUT/mcfm-launch"; do codesign -f -s - "$f" 2>/dev/null; done
echo "make_launcher: $OUT ready ($(ls "$OUT"/mcfm_stub_*.dylib | wc -l | tr -d ' ') stub libraries)"
