#!/bin/bash
# usage: lib_test.sh <libmcfm.so> <ndk>
# Checks the Android library: ARM shared object, JNI entry point, no link-time
# dependency on the game (everything is found with dlsym at runtime).
set -uo pipefail
LIB="${1:?usage: lib_test.sh <libmcfm.so> <ndk>}"; NDK="${2:?ndk path}"
BIN="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin"
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
[ -f "$LIB" ] || { echo "FAIL: $LIB not built"; exit 1; }
file "$LIB" | grep -q 'ELF 32-bit LSB shared object, ARM' || fail "not a 32-bit ARM shared object: $(file "$LIB")"
"$BIN/arm-linux-androideabi-nm" -D --defined-only "$LIB" | grep -q ' T JNI_OnLoad$' || fail "JNI_OnLoad not exported"
NEEDED="$("$BIN/arm-linux-androideabi-readelf" -d "$LIB" | grep NEEDED)"
grep -q 'libminecraftpe' <<<"$NEEDED" && fail "links against libminecraftpe.so (must be dlsym only)"
grep -q 'libgnustl_shared.so' <<<"$NEEDED" || fail "not built against gnustl_shared (std::string ABI)"
strings "$LIB" | grep -q '_ZTV21AppPlatform_android23' || fail "AppPlatform vtable symbol name missing"
# Startup must not depend on Android's private target-SDK functions (absent/private on
# many versions; a bypass could abort the game).
strings "$LIB" | grep -q 'application_target_sdk_version' && fail "uses private target-SDK APIs"
# All logs go to the documented tag (adb logcat -s mcfm).
strings "$LIB" | grep -qx 'mcfm' || fail "no mcfm log tag"
# Only the vtable page is made writable; the game's code mappings are left alone.
strings "$LIB" | grep -q '/proc/self/maps' && fail "rewrites libminecraftpe mappings from /proc/self/maps"
[ $fails = 0 ] && echo "lib_test: passed" || { echo "$fails failure(s)"; exit 1; }
