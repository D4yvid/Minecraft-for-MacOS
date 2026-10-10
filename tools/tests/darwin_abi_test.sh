#!/bin/bash
# The committed Darwin ABI tables (android/launcher/darwin/darwin_abi.h, darwin_ctype.inc) are
# exactly what tools/android/darwin_abi_gen.c prints from the macOS SDK.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
clang -arch arm64 -Wall -Werror "$ROOT/tools/android/darwin_abi_gen.c" -o "$T/gen" || { echo "FAIL: generator does not build"; exit 1; }
"$T/gen" header > "$T/darwin_abi.h" && "$T/gen" ctype > "$T/darwin_ctype.inc" || { echo "FAIL: generator failed"; exit 1; }
for f in darwin_abi.h darwin_ctype.inc; do
  diff -q "$T/$f" "$ROOT/android/launcher/darwin/$f" >/dev/null 2>&1 \
    || { echo "FAIL: android/launcher/darwin/$f is stale (make darwin-abi)"; fails=$((fails+1)); }
done
grep -q "constexpr long kEAGAIN = 35L;" "$T/darwin_abi.h" || { echo "FAIL: EAGAIN is not Darwin's 35"; fails=$((fails+1)); }
grep -q "constexpr long k_PTHREAD_MUTEX_SIG_init = 0x32AAABA7L;" "$T/darwin_abi.h" || { echo "FAIL: mutex initializer signature"; fails=$((fails+1)); }
grep -q "constexpr long kSizeof_stat = 144L;" "$T/darwin_abi.h" || { echo "FAIL: struct stat is not 144 bytes"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "darwin_abi_test: passed" || { echo "$fails failure(s)"; exit 1; }
