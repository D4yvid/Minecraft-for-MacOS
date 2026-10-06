#!/bin/bash
# check_apk_lib.sh must accept only a 0.15.10 libminecraftpe.so (the symbols we patch).
# usage: check_apk_lib_test.sh [real libminecraftpe.so]   (real case skipped without it)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
printf 'not a game library\n' > "$T/other.so"
if bash "$ROOT/android/tools/check_apk_lib.sh" "$T/other.so" >/dev/null 2>&1; then echo "FAIL: accepted a foreign library"; fails=$((fails+1)); fi
if bash "$ROOT/android/tools/check_apk_lib.sh" "$T/missing.so" >/dev/null 2>&1; then echo "FAIL: accepted a missing file"; fails=$((fails+1)); fi
msg="$(bash "$ROOT/android/tools/check_apk_lib.sh" "$T/other.so" 2>&1)"
grep -q '0.15.10' <<<"$msg" || { echo "FAIL: message does not name the supported build"; fails=$((fails+1)); }
if [ -n "${1:-}" ]; then
  bash "$ROOT/android/tools/check_apk_lib.sh" "$1" >/dev/null || { echo "FAIL: rejected the real library"; fails=$((fails+1)); }
fi
[ $fails = 0 ] && echo "check_apk_lib_test: passed" || { echo "$fails failure(s)"; exit 1; }
