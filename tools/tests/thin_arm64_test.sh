#!/bin/bash
# thin_arm64.sh: a fat binary is thinned to arm64, an arm64-only binary (what many decryptors
# produce) is copied as is, a binary without arm64 is refused.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T"
printf 'int main(void){return 0;}\n' > "$T/m.c"
clang -arch arm64 "$T/m.c" -o "$T/arm64"
clang -arch x86_64 "$T/m.c" -o "$T/x86_64"
lipo -create "$T/arm64" "$T/x86_64" -output "$T/fat"
fails=0
for input in fat arm64; do
  bash "$ROOT/tools/launcher/thin_arm64.sh" "$T/$input" "$T/out-$input" || { echo "FAIL: $input refused"; fails=$((fails+1)); continue; }
  [ "$(lipo -archs "$T/out-$input")" = arm64 ] || { echo "FAIL: $input not thinned to arm64"; fails=$((fails+1)); }
done
bash "$ROOT/tools/launcher/thin_arm64.sh" "$T/x86_64" "$T/out-x86" 2>"$T/err" && { echo "FAIL: x86_64-only accepted"; fails=$((fails+1)); }
grep -q "no arm64" "$T/err" || { echo "FAIL: no 'no arm64' message"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "thin_arm64_test: passed" || { echo "$fails failure(s)"; exit 1; }
