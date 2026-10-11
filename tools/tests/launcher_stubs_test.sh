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
// Return-register contract: like a message to nil, a stub zeroes x0, x1 and d0-d3.
typedef struct { double a, b, c, d; } rect4;  // HFA: returned in d0-d3 (CGRect)
typedef struct { long a, b; } pair;            // returned in x0, x1 (NSRange)
__attribute__((noinline)) static void dirty(void) {
  __asm__ volatile("fmov d0, #1.5\n fmov d1, #1.5\n fmov d2, #1.5\n fmov d3, #1.5\n mov x1, #0x55"
                   ::: "d0", "d1", "d2", "d3", "x1");
}
static void *worker(void *arg) { (void)arg; for (int i = 0; i < 1000; i++) fakekit_hello(); return 0; }
int main(void) {
  pthread_t t[8];
  for (int i = 0; i < 8; i++) pthread_create(&t[i], 0, worker, 0);
  for (int i = 0; i < 8; i++) pthread_join(t[i], 0);
  for (int i = 0; i < 256; i++) if (kFakeKitValue[i]) { puts("data not zero"); return 1; }
  if (fakekit_hello() != 0) { puts("fn not 0"); return 1; }
  if (objc_msgSend(0, "poke") || objc_msgSend(0, "poke") || objc_msgSend(0, "other")) { puts("msgSend not 0"); return 1; }
  if (objc_msgSend(0, 0) || objc_msgSend(0, 0)) { puts("msgSend(nil sel) not 0"); return 1; }
  // Many distinct selectors: the log is bounded (4096 + one "full" line), calls stay cheap.
  static char names[6000][16];
  for (int i = 0; i < 6000; i++) { snprintf(names[i], sizeof names[i], "sel%d", i); objc_msgSend(0, names[i]); }
  for (int r = 0; r < 100; r++) for (int i = 0; i < 6000; i++) if (objc_msgSend(0, names[i])) return 1;
  dirty(); double d = ((double (*)(void))fakekit_hello)();
  if (d != 0.0) { printf("double return not 0: %g\n", d); return 1; }
  dirty(); rect4 r = ((rect4 (*)(void))fakekit_hello)();
  if (r.a || r.b || r.c || r.d) { printf("rect return not 0: %g %g %g %g\n", r.a, r.b, r.c, r.d); return 1; }
  dirty(); pair p = ((pair (*)(void))fakekit_hello)();
  if (p.a || p.b) { printf("pair return not 0: %ld %ld\n", p.a, p.b); return 1; }
  dirty(); rect4 m = ((rect4 (*)(void *, const char *))objc_msgSend)(0, "bounds");
  if (m.a || m.b || m.c || m.d) { puts("msgSend rect return not 0"); return 1; }
  return 0;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/use.c" "$T/stubs/mcfm_stub_FakeKit.dylib" \
  "$T/stubs/mcfm_stub_libobjc.dylib" -Wl,-rpath,"$T/stubs" -o "$T/use" || { echo "FAIL: link"; exit 1; }
ERR="$(MCFM_CENSUS="$T/census.txt" "$T/use" 2>&1 >"$T/stdout")" || { echo "FAIL: use exited $?: $(cat "$T/stdout")"; fails=$((fails+1)); }
count() { grep -cxF "$1" <<<"$ERR"; }
[ "$(count 'mcfm: stub FakeKit:_fakekit_hello')" = 1 ] || { echo "FAIL: fakekit_hello not logged exactly once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend poke')" = 1 ] || { echo "FAIL: poke not logged once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend other')" = 1 ] || { echo "FAIL: other not logged once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend (null)')" = 1 ] || { echo "FAIL: nil selector not logged once"; fails=$((fails+1)); }
SELS="$(grep -c '^mcfm: stub libobjc:_objc_msgSend sel' <<<"$ERR")"
[ "$SELS" = 4093 ] || { echo "FAIL: $SELS selector lines, want 4093 (4096 minus poke, other, (null))"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc: selector log full (4096), further selectors not logged')" = 1 ] || { echo "FAIL: no single log-full line"; fails=$((fails+1)); }
CENSUS="$(cat "$T/census.txt" 2>/dev/null)"
[ "$(grep -vc ' sel[0-9]' <<<"$CENSUS")" = 5 ] || { echo "FAIL: census should have 5 non-sel lines (bounds comes after the log is full)"; fails=$((fails+1)); }
grep -qxF 'FakeKit:_fakekit_hello' <<<"$CENSUS" || { echo "FAIL: census missing fakekit_hello"; fails=$((fails+1)); }
# Android (Stage 3a): the same stubs as ELF .so files for arm64, 16 KB-aligned, named by soname.
ANDROID_CC="${ANDROID_CC:-$HOME/Library/Android/sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang}"
if [ -x "$ANDROID_CC" ]; then
  READELF="$(dirname "$ANDROID_CC")/llvm-readelf"
  ANDROID_CC="$ANDROID_CC" bash "$ROOT/tools/launcher/build_stubs.sh" --target android "$T/imports.tsv" "$T/astubs" >/dev/null \
    || { echo "FAIL: build_stubs --target android"; fails=$((fails+1)); }
  for so in libmcfm_stub_FakeKit.so libmcfm_stubrt.so; do
    H="$("$READELF" -h "$T/astubs/$so" 2>/dev/null)"
    grep -q "Machine:.*AArch64" <<<"$H" && grep -q "Class:.*ELF64" <<<"$H" || { echo "FAIL: $so is not an arm64 ELF"; fails=$((fails+1)); }
    "$READELF" -lW "$T/astubs/$so" | awk '$1=="LOAD" && $NF!="0x4000" {bad=1} END {exit bad}' || { echo "FAIL: $so LOAD alignment is not 16 KB"; fails=$((fails+1)); }
    DYN="$("$READELF" -d "$T/astubs/$so")"  # no grep -q in a pipe: SIGPIPE under pipefail
    grep -q "SONAME.*\[$so\]" <<<"$DYN" || { echo "FAIL: $so soname"; fails=$((fails+1)); }
  done
  SYMS="$("$READELF" -sW --dyn-syms "$T/astubs/libmcfm_stub_FakeKit.so")"
  grep -q " _fakekit_hello$" <<<"$SYMS" || { echo "FAIL: stub keeps the Mach-O name"; fails=$((fails+1)); }
else
  echo "launcher_stubs_test: NDK r27d not found, Android stubs not checked"
fi
# --target ios (Stage 4): iOS dylibs from the iPhoneOS SDK; --skip leaves a library to the system.
if DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}" xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1; then
  export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
  if bash "$ROOT/tools/launcher/build_stubs.sh" --target ios "$T/imports.tsv" "$T/ios" --skip EmptyKit >/dev/null; then
    for f in "$T"/ios/*.dylib; do
      vtool -show-build "$f" 2>/dev/null | grep -q 'platform IOS$' || { echo "FAIL: $(basename "$f") is not an iOS build"; fails=$((fails+1)); }
    done
    [ -e "$T/ios/mcfm_stub_FakeKit.dylib" ] && [ -e "$T/ios/libmcfm_stubrt.dylib" ] || { echo "FAIL: iOS stubs missing"; fails=$((fails+1)); }
    [ -e "$T/ios/mcfm_stub_EmptyKit.dylib" ] && { echo "FAIL: --skip EmptyKit still built a stub"; fails=$((fails+1)); }
  else
    echo "FAIL: build_stubs --target ios"; fails=$((fails+1))
  fi
else
  echo "launcher_stubs_test: iOS stubs skipped (no iPhoneOS SDK)"
fi
[ $fails = 0 ] && echo "launcher_stubs_test: passed" || { echo "$fails failure(s)"; exit 1; }
