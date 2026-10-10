#!/bin/bash
# pick_gnustl.sh keeps the game's C++ runtime when it covers libmcfm's needs.
# usage: pick_gnustl_test.sh <ndk> <libs dir from make android> [game libgnustl_shared.so]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK="${1:?ndk}"; LIBS="${2:?libs dir}"; GAME_STL="${3:-}"
fails=0
pick() { NDK="$NDK" bash "$ROOT/android/tools/pick_gnustl.sh" "$1" "$LIBS/libgnustl_shared.so" "$LIBS/libmcfm.so"; }
# a "runtime" that exports none of the C++ symbols libmcfm needs -> use the NDK one
[ "$(pick "$LIBS/libmcfm.so")" = "$LIBS/libgnustl_shared.so" ] || { echo "FAIL: kept an incompatible runtime"; fails=$((fails+1)); }
# no runtime in the APK -> NDK one
[ "$(pick /nonexistent/libgnustl_shared.so)" = "$LIBS/libgnustl_shared.so" ] || { echo "FAIL: missing runtime not replaced"; fails=$((fails+1)); }
# the NDK runtime itself is always sufficient -> kept
[ "$(pick "$LIBS/libgnustl_shared.so")" = "$LIBS/libgnustl_shared.so" ] || { echo "FAIL: rejected a compatible runtime"; fails=$((fails+1)); }
if [ -n "$GAME_STL" ]; then
  [ "$(pick "$GAME_STL")" = "$GAME_STL" ] || { echo "FAIL: replaced the game's compatible runtime"; fails=$((fails+1)); }
fi
[ $fails = 0 ] && echo "pick_gnustl_test: passed" || { echo "$fails failure(s)"; exit 1; }
