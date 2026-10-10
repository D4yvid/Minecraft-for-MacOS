#!/bin/bash
# make app/run/check build and run the Mach-O launcher; the deprecated Mac Catalyst build
# stays available as catalyst/catalyst-run/catalyst-check and says it is deprecated.
# Dry runs only (make -n).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fails=0
dry() { make -n "$1" GAME=/nonexistent/minecraftpe2.app 2>&1; }
runs() {  # target, text the dry run must contain
  case "$(dry "$1")" in *"$2"*) ;; *) echo "FAIL: make $1 does not run '$2'"; fails=$((fails+1)) ;; esac
}
deprecated() {  # target, yes|no
  local out; out="$(dry "$1")"
  case "$out" in
    *deprecated*) [ "$2" = yes ] || { echo "FAIL: make $1 says deprecated"; fails=$((fails+1)); } ;;
    *) [ "$2" = no ] || { echo "FAIL: make $1 does not say deprecated"; fails=$((fails+1)); } ;;
  esac
}
runs catalyst macos/tools/convert.sh;        deprecated catalyst yes
runs catalyst-run "open ";                   deprecated catalyst-run yes
runs catalyst-check macos/tests/smoke.sh;    deprecated catalyst-check yes
runs app macos/tools/make_launcher.sh;       deprecated app no
runs run mcfm-launch;                        deprecated run no
runs check "--frames 120";                   deprecated check no
# The launcher targets refuse while the game runs (rebuilding dist/launcher or starting a
# second instance on the same worlds would break the running one).
runs app "pgrep -x mcfm-launch"
runs check "pgrep -x mcfm-launch"
runs run "pgrep -x mcfm-launch"
[ $fails = 0 ] && echo "make_targets_test: passed" || { echo "$fails failure(s)"; exit 1; }
