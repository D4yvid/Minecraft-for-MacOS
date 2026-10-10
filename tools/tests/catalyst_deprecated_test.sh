#!/bin/bash
# The Mac Catalyst build is a deprecated build mode: its targets still work (also under the
# old names app/run/check) and say they are deprecated. Dry runs only (make -n).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fails=0
expect() {  # target, text the dry run must contain
  local out
  out="$(make -n "$1" GAME=/nonexistent/minecraftpe2.app 2>&1)"
  case "$out" in
    *"$2"*) ;;
    *) echo "FAIL: make $1 does not run '$2'"; fails=$((fails+1)) ;;
  esac
  case "$out" in
    *"deprecated"*) ;;
    *) echo "FAIL: make $1 does not say the Catalyst build is deprecated"; fails=$((fails+1)) ;;
  esac
}
expect catalyst macos/tools/convert.sh
expect app macos/tools/convert.sh
expect catalyst-run "open "
expect run "open "
expect catalyst-check macos/tests/smoke.sh
expect check macos/tests/smoke.sh
[ $fails = 0 ] && echo "catalyst_deprecated_test: passed" || { echo "$fails failure(s)"; exit 1; }
