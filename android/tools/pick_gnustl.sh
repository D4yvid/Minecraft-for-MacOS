#!/bin/bash
# usage: NDK=<ndk> pick_gnustl.sh <game libgnustl_shared.so> <NDK libgnustl_shared.so> <librunet.so>
# Prints which libgnustl_shared.so to ship: the game's own when it exports every C++
# runtime symbol librunet.so imports (so the engine keeps the runtime it was built with),
# otherwise the NDK's.
set -uo pipefail
GAME_STL="$1"; NDK_STL="$2"; RUNET="$3"
NM="$(ls "${NDK:-}"/toolchains/arm-linux-androideabi-4.9/prebuilt/*/bin/arm-linux-androideabi-nm 2>/dev/null | head -1)"
if [ ! -f "$GAME_STL" ] || [ -z "$NM" ]; then echo "$NDK_STL"; exit 0; fi
syms() { "$NM" -D "$@" 2>/dev/null | awk '{print $NF}' | sort -u; }
NEEDED="$(comm -12 <(syms -u "$RUNET") <(syms --defined-only "$NDK_STL"))"
MISSING="$(comm -23 <(echo "$NEEDED") <(syms --defined-only "$GAME_STL"))"
if [ -z "$MISSING" ]; then echo "$GAME_STL"; else echo "$NDK_STL"; fi
