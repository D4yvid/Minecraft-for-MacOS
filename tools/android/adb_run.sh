#!/bin/bash
# usage: adb_run.sh [--push <file>]... <local binary> [args...]
# Runs a binary on the connected device/emulator from /data/local/tmp/mcfm/ (pushing it and any
# --push files there first), prints its output, and exits with the binary's own exit code
# (adb shell's is unreliable on old devices). Arguments naming pushed files can use @DIR@ for
# the remote directory.
set -euo pipefail
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="${ADB:-$SDK/platform-tools/adb}"
DIR=/data/local/tmp/mcfm
PUSH=()
while [ "${1:-}" = "--push" ]; do PUSH+=("$2"); shift 2; done
[ $# -ge 1 ] || { echo "usage: adb_run.sh [--push <file>]... <binary> [args...]" >&2; exit 2; }
BIN="$1"; shift
"$ADB" get-state >/dev/null 2>&1 || { echo "adb_run: no device (make android-emulator)" >&2; exit 2; }
"$ADB" shell mkdir -p "$DIR" >/dev/null
for f in "$BIN" ${PUSH[@]+"${PUSH[@]}"}; do "$ADB" push "$f" "$DIR/" >/dev/null 2>&1 || { echo "adb_run: cannot push $f" >&2; exit 2; }; done
ARGS=()
for a in "$@"; do ARGS+=("'${a//@DIR@/$DIR}'"); done
OUT="$("$ADB" shell "cd $DIR && chmod 755 ./$(basename "$BIN") && LD_LIBRARY_PATH=$DIR ./$(basename "$BIN") ${ARGS[*]-} 2>&1; echo \"mcfm-exit:\$?\"" | tr -d '\r')"
printf '%s\n' "$OUT" | sed '$d'
CODE="$(printf '%s\n' "$OUT" | tail -1 | sed -n 's/^mcfm-exit:\([0-9]*\)$/\1/p')"
[ -n "$CODE" ] || { echo "adb_run: no exit status from the device" >&2; exit 2; }
exit "$CODE"
