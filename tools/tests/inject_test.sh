#!/bin/bash
# Builds a throwaway binary, injects /usr/lib/libz.1.dylib, checks it loads.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"  # repo root
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
printf 'int main(void){return 0;}\n' > "$T/hello.c"
clang -arch arm64 -Wl,-headerpad,0x400 "$T/hello.c" -o "$T/hello"
python3 -I "$ROOT/tools/inject.py" "$T/hello" /usr/lib/libz.1.dylib
python3 -I "$ROOT/tools/inject.py" "$T/hello" /usr/lib/libz.1.dylib   # idempotent
n=$(otool -L "$T/hello" | grep -c 'libz.1.dylib' || true)
[ "$n" = "1" ] || { echo "FAIL: expected 1 libz load command, got $n"; exit 1; }
codesign -f -s - "$T/hello" 2>/dev/null
LOADED="$(DYLD_PRINT_LIBRARIES=1 "$T/hello" 2>&1)"
grep -q "/usr/lib/libz.1.dylib" <<<"$LOADED" || { echo "FAIL: libz not loaded at runtime"; exit 1; }
echo "inject_test: passed"
