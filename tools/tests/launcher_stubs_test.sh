#!/bin/bash
# Generated stubs: return 0, data is zero, each symbol / selector logged once (also across
# threads), census file written.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T"
printf 'EmptyKit\t-\tlib\nFakeKit\t-\tlib\nFakeKit\t_fakekit_hello\tfn\nFakeKit\t_kFakeKitValue\tdata\nlibobjc\t_objc_msgSend\tfn\nlibSystem\t-\tlib\nlibSystem\t_malloc\tfn\n' > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T/stubs" || { echo "FAIL: build_stubs"; exit 1; }
fails=0
[ -e "$T/stubs/mcfm_stub_libSystem.dylib" ] && { echo "FAIL: host library got a stub"; fails=$((fails+1)); }
[ -e "$T/stubs/mcfm_stub_EmptyKit.dylib" ] || { echo "FAIL: library without symbols got no stub"; fails=$((fails+1)); }
cat > "$T/use.c" <<'EOF'
#include <pthread.h>
#include <stdio.h>
int fakekit_hello(void);
extern char kFakeKitValue[256];
void *objc_msgSend(void *self, const char *sel);
static void *worker(void *arg) { (void)arg; for (int i = 0; i < 1000; i++) fakekit_hello(); return 0; }
int main(void) {
  pthread_t t[8];
  for (int i = 0; i < 8; i++) pthread_create(&t[i], 0, worker, 0);
  for (int i = 0; i < 8; i++) pthread_join(t[i], 0);
  for (int i = 0; i < 256; i++) if (kFakeKitValue[i]) { puts("data not zero"); return 1; }
  if (fakekit_hello() != 0) { puts("fn not 0"); return 1; }
  if (objc_msgSend(0, "poke") || objc_msgSend(0, "poke") || objc_msgSend(0, "other")) { puts("msgSend not 0"); return 1; }
  return 0;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/use.c" "$T/stubs/mcfm_stub_FakeKit.dylib" \
  "$T/stubs/mcfm_stub_libobjc.dylib" -Wl,-rpath,"$T/stubs" -o "$T/use" || { echo "FAIL: link"; exit 1; }
ERR="$(MCFM_CENSUS="$T/census.txt" "$T/use" 2>&1 >/dev/null)" || { echo "FAIL: use exited $?: $ERR"; fails=$((fails+1)); }
count() { grep -cxF "$1" <<<"$ERR"; }
[ "$(count 'mcfm: stub FakeKit:_fakekit_hello')" = 1 ] || { echo "FAIL: fakekit_hello not logged exactly once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend poke')" = 1 ] || { echo "FAIL: poke not logged once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend other')" = 1 ] || { echo "FAIL: other not logged once"; fails=$((fails+1)); }
CENSUS="$(cat "$T/census.txt" 2>/dev/null)"
[ "$(wc -l <<<"$CENSUS" | tr -d ' ')" = 3 ] || { echo "FAIL: census should have 3 lines: $CENSUS"; fails=$((fails+1)); }
grep -qxF 'FakeKit:_fakekit_hello' <<<"$CENSUS" || { echo "FAIL: census missing fakekit_hello"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_stubs_test: passed" || { echo "$fails failure(s)"; exit 1; }
