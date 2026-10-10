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
# 2. load_image with a fake OS: every fixup slot, weak binds, hooks, unwind, errors.
make -s build/test/loader_core_test >/dev/null || { echo "FAIL: build loader_core_test"; exit 1; }
build/test/loader_core_test "$T/libminecraftpe.dylib" "$T/symbols.txt" || fails=$((fails+1))
# 3. On macOS for real: signed mapping, symbols, unwind; the fixture's code runs. The
#    initializer sets 7 + fakekit_hello() - 1 = 6 (the FakeKit stub returns 0), so the
#    exception path returns 35 + 6; the hook answers 1000 + x. The initializer itself catches an
#    exception and calls the hooked function with argc (3): 1003.
make -s build/test/loader_run >/dev/null || { echo "FAIL: build loader_run"; exit 1; }
RUN="$(build/test/loader_run "$T/libminecraftpe.dylib" "$T/symbols.txt" 2>&1)" || { echo "FAIL: loader_run: $RUN"; fails=$((fails+1)); }
grep -qx "thrower=41 answer=1005 init_answer=1003" <<<"$RUN" || { echo "FAIL: fixture code under our loader: $RUN"; fails=$((fails+1)); }
grep -qx "operator_new=libc++" <<<"$RUN" || { echo "FAIL: operator new not libc++'s: $RUN"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "loader_test: passed" || { echo "$fails failure(s)"; exit 1; }
