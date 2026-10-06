#!/bin/bash
# usage: bundle_test.sh <converted.app> — checks naming of the installed bundle.
set -uo pipefail
APP="$1"; fails=0
check() { [ "$2" = "$3" ] || { echo "FAIL: $1: got '$2', want '$3'"; fails=$((fails+1)); }; }
P="$APP/Info.plist"
check "app folder" "$(basename "$APP")" "minecraftpe.app"
check "CFBundleExecutable" "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$P" 2>/dev/null)" "minecraftpe"
check "CFBundleDisplayName" "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDisplayName' "$P" 2>/dev/null)" "Minecraft PE"
check "CFBundleIdentifier (keeps settings)" "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$P" 2>/dev/null)" "com.mojang.minecraftpe2"
[ -x "$APP/minecraftpe" ] || { echo "FAIL: no executable minecraftpe"; fails=$((fails+1)); }
[ ! -e "$APP/minecraftpe2" ] || { echo "FAIL: old executable minecraftpe2 still present"; fails=$((fails+1)); }
otool -L "$APP/minecraftpe" 2>/dev/null | grep -q libmcpekbm || { echo "FAIL: mod not injected"; fails=$((fails+1)); }
codesign -v "$APP" 2>/dev/null || { echo "FAIL: bundle signature invalid"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "bundle_test: passed" || { echo "$fails failure(s)"; exit 1; }
