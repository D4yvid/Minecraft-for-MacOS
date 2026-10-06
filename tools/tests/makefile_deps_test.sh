#!/bin/bash
# Editing any shared header must make the platform dylibs out of date.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fails=0
make -s macos >/dev/null || { echo "FAIL: cannot build"; exit 1; }
DYLIB=build/macos/libmcfm.dylib
HEADERS="$(git ls-files 'shared/*.h' 'macos/src/*.h' | grep -v '^shared/tests/')"
# Explicit timestamps (make 3.81 compares whole seconds): every header older than the
# dylib, then one header at a time newer.
setmtime() { python3 -I -c 'import os,sys; t=float(sys.argv[1]); [os.utime(f,(t,t)) for f in sys.argv[2:]]' "$@"; }
T="$(python3 -I -c 'import time; print(int(time.time()))')"
setmtime $((T - 10)) $HEADERS
setmtime "$T" "$DYLIB"
make -q "$DYLIB" 2>/dev/null || { echo "FAIL: dylib out of date with no edits"; fails=$((fails+1)); }
for h in $HEADERS; do
  setmtime $((T + 2)) "$h"
  if make -q "$DYLIB" 2>/dev/null; then echo "FAIL: macOS dylib not rebuilt after editing $h"; fails=$((fails+1)); fi
  setmtime $((T - 10)) "$h"
done
[ $fails = 0 ] && echo "makefile_deps_test: passed" || { echo "$fails failure(s)"; exit 1; }
