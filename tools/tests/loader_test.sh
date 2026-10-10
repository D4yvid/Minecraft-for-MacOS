#!/bin/bash
# Our loader on the loader fixture (tools/tests/loader_fixture.sh):
# 1. the fixup decoder equals dyld_info for rebases, binds and lazy binds, and finds the
#    weak binds (dyld_info prints those with stale symbol names, so they are checked by name).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash tools/tests/loader_fixture.sh "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
make -s build/test/fixups_dump >/dev/null || { echo "FAIL: build fixups_dump"; exit 1; }
fails=0
OURS="$(build/test/fixups_dump "$T/libminecraftpe.dylib")" || { echo "FAIL: fixups_dump"; exit 1; }
WEAK="$(grep -c '^weak-bind ' <<<"$OURS")"
norm_dyld() { awk 'NR>3 && NF>=5 {t=$4; a=$3; if (t == "rebase") {print t, a; next} $1=$2=$3=$4=""; sub(/^ +/, ""); print t, a, $0}'; }
DYLD="$(dyld_info -fixups "$T/libminecraftpe.dylib" | norm_dyld)"
DYLD_NO_WEAK="$(head -n $(( $(wc -l <<<"$DYLD") - WEAK )) <<<"$DYLD")"
[ "$(grep -v '^weak-bind ' <<<"$OURS" | sort)" = "$(sort <<<"$DYLD_NO_WEAK")" ] \
  || { echo "FAIL: rebases/binds differ from dyld_info"; diff <(grep -v '^weak-bind ' <<<"$OURS" | sort) <(sort <<<"$DYLD_NO_WEAK") | head; fails=$((fails+1)); }
grep -q '^weak-bind .*/__Znwm$' <<<"$OURS" || { echo "FAIL: weak bind of operator new missing"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "loader_test: passed" || { echo "$fails failure(s)"; exit 1; }
