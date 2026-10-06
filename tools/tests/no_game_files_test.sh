#!/bin/bash
# The repo must never contain Mojang's files. Checks `git ls-files` (or a list on stdin
# with --stdin) against forbidden patterns; also self-tests the patterns.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
bad() { grep -iE '\.(ipa|apk|smali|dex)$|\.app/|(^|/)lib(minecraftpe|fmod|ovrfmod|ovrplatformloader)\.so$|(^|/)minecraftpe2?$|(^|/)(assets|res)/.*\.(png|ogg|fsb|json|lang|material)$|(^|/)Payload/'; }
# self-test: a list with game files must be caught, a clean list must pass
printf 'src/a.cpp\nPayload/minecraftpe2.app/minecraftpe2\nx/lib/armeabi-v7a/libminecraftpe.so\nApplication/smali/Main.smali\nfoo.apk\n' | bad | wc -l | grep -qx ' *4' \
  || { echo "FAIL: forbidden-pattern self-test"; exit 1; }
printf 'shared/src/keymap.cpp\nandroid/jni/main.cpp\ndocs/ARCHITECTURE.md\n' | bad | grep -q . && { echo "FAIL: clean list flagged"; exit 1; }
FOUND="$(git -C "$ROOT" ls-files | bad)"
[ -z "$FOUND" ] || { echo "FAIL: game files are tracked:"; echo "$FOUND"; exit 1; }
echo "no_game_files_test: passed"
