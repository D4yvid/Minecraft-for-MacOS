#!/bin/bash
# Editing any shared header must make the platform dylibs out of date.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fails=0
make -s macos >/dev/null || { echo "FAIL: cannot build"; exit 1; }
DYLIB=build/macos/libmcfm.dylib
HEADERS="$(git ls-files 'shared/*.h' 'macos/src/*.h' | grep -v -e '^shared/tests/' -e '^shared/launcher/' -e '^shared/loader/')"  # launcher/loader headers are not in the Catalyst dylib
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
# The same for the launcher: every header it compiles against must make mcfm-launch stale.
make -s build/launcher/mcfm-launch >/dev/null || { echo "FAIL: cannot build mcfm-launch"; exit 1; }
LBIN=build/launcher/mcfm-launch
LHEADERS="$(git ls-files 'shared/apple/*.h' 'shared/include/*.h' 'shared/launcher/*.h' 'shared/loader/*.h' 'macos/launcher/*.h' | grep -v 'stub_runtime.h')"
setmtime $((T - 10)) $LHEADERS
setmtime "$T" "$LBIN"
make -q "$LBIN" 2>/dev/null || { echo "FAIL: mcfm-launch out of date with no edits"; fails=$((fails+1)); }
for h in $LHEADERS; do
  setmtime $((T + 2)) "$h"
  if make -q "$LBIN" 2>/dev/null; then echo "FAIL: mcfm-launch not rebuilt after editing $h"; fails=$((fails+1)); fi
  setmtime $((T - 10)) "$h"
done
[ $fails = 0 ] && echo "makefile_deps_test: passed" || { echo "$fails failure(s)"; exit 1; }
