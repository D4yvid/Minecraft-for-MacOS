#!/bin/bash
# mcfm_image.py dylib: the iOS-tagged fixture executable becomes a dylib that macOS dyld
# loads with every non-host library stubbed; libobjc never sees its ObjC metadata; its
# static initializer runs and reaches the stubs.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL=(python3 -I "$ROOT/tools/launcher/mcfm_image.py")
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" || { echo "FAIL: fixture"; exit 1; }
fails=0
"${TOOL[@]}" imports "$T/fixture" > "$T/imports.tsv" || { echo "FAIL: imports"; exit 1; }
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T" || { echo "FAIL: stubs"; exit 1; }
"${TOOL[@]}" dylib "$T/fixture" "$T/libminecraftpe.dylib" || { echo "FAIL: dylib exited $?"; exit 1; }
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null || { echo "FAIL: codesign"; exit 1; }
HDR="$(otool -hv "$T/libminecraftpe.dylib")"; CMDS="$(otool -l "$T/libminecraftpe.dylib")"; LIBS="$(otool -L "$T/libminecraftpe.dylib")"
grep -q " DYLIB " <<<"$HDR" || { echo "FAIL: not MH_DYLIB"; fails=$((fails+1)); }
grep -q " PIE" <<<"$HDR" && { echo "FAIL: PIE flag kept"; fails=$((fails+1)); }
grep -q "@rpath/libminecraftpe.dylib" <<<"$LIBS" || { echo "FAIL: no LC_ID_DYLIB"; fails=$((fails+1)); }
grep -q "@rpath/mcfm_stub_FakeKit.dylib" <<<"$LIBS" || { echo "FAIL: FakeKit not redirected"; fails=$((fails+1)); }
grep -q "@rpath/mcfm_stub_libobjc.dylib" <<<"$LIBS" || { echo "FAIL: libobjc not redirected"; fails=$((fails+1)); }
grep -q "/usr/lib/libSystem.B.dylib" <<<"$LIBS" || { echo "FAIL: libSystem not kept"; fails=$((fails+1)); }
grep -qE "cmd LC_(MAIN|LOAD_DYLINKER)$" <<<"$CMDS" && { echo "FAIL: LC_MAIN/LC_LOAD_DYLINKER kept"; fails=$((fails+1)); }
grep -q "sectname __objc_" <<<"$CMDS" && { echo "FAIL: __objc_ sections visible"; fails=$((fails+1)); }
grep -q "segname __MCFM_PAD" <<<"$CMDS" || { echo "FAIL: no __MCFM_PAD"; fails=$((fails+1)); }
grep -A3 "cmd LC_BUILD_VERSION" <<<"$CMDS" | grep -qE "platform (1|MACOS|macos)$" || { echo "FAIL: not retagged to macOS"; fails=$((fails+1)); }
# Load it: the fixture's initializer must reach the stubs (and not crash in libobjc).
cat > "$T/host.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
  void *h = dlopen(argv[1], RTLD_NOW);
  if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  return 0;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/host.c" -Wl,-rpath,@executable_path -o "$T/host"  # like mcfm-launch
OUT="$(MCFM_CENSUS="$T/census.txt" "$T/host" "$T/libminecraftpe.dylib" 2>&1)" || { echo "FAIL: dlopen: $OUT"; fails=$((fails+1)); }
grep -qxF "FakeKit:_fakekit_hello" "$T/census.txt" 2>/dev/null || { echo "FAIL: initializer did not reach FakeKit stub"; fails=$((fails+1)); }
grep -qxF "libobjc:_objc_msgSend poke" "$T/census.txt" 2>/dev/null || { echo "FAIL: initializer did not reach objc_msgSend stub"; fails=$((fails+1)); }
# Already a dylib -> refused, nothing written.
"${TOOL[@]}" dylib "$T/libminecraftpe.dylib" "$T/again.dylib" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "not an executable" "$T/err" && [ ! -e "$T/again.dylib" ]; } || { echo "FAIL: dylib input not refused (rc $rc)"; fails=$((fails+1)); }
# No header padding -> either a loadable result or a clean "header padding" refusal.
mkdir -p "$T/tight"
FIXTURE_LDFLAGS="-Wl,-headerpad,0" bash "$ROOT/tools/tests/launcher_fixture.sh" "$T/tight" >/dev/null 2>&1
"${TOOL[@]}" dylib "$T/tight/fixture" "$T/tight/out.dylib" 2>"$T/err"; rc=$?
if [ $rc = 2 ]; then
  { grep -q "header padding" "$T/err" && [ ! -e "$T/tight/out.dylib" ]; } || { echo "FAIL: tight header: bad refusal"; fails=$((fails+1)); }
elif [ $rc = 0 ]; then
  otool -l "$T/tight/out.dylib" >/dev/null 2>&1 || { echo "FAIL: tight header: corrupt output"; fails=$((fails+1)); }
else
  echo "FAIL: tight header: exit $rc"; fails=$((fails+1))
fi
# Load commands that must grow past the padding (8 one-letter install names -> long stub
# paths) -> refused with "header padding", nothing written.
mkdir -p "$T/grow"
for i in 1 2 3 4 5 6 7 8; do
  echo "int f$i(void){return $i;}" > "$T/grow/l$i.c"
  clang -arch arm64 -mmacosx-version-min=11.0 -dynamiclib "$T/grow/l$i.c" -install_name "/l$i" -o "$T/grow/l$i.dylib"
done
printf 'int f1(void),f2(void),f3(void),f4(void),f5(void),f6(void),f7(void),f8(void);\nint main(void){return f1()+f2()+f3()+f4()+f5()+f6()+f7()+f8();}\n' > "$T/grow/m.c"
clang -arch arm64 -mmacosx-version-min=11.0 -Wl,-headerpad,0 -Wl,-no_fixup_chains "$T/grow/m.c" "$T"/grow/l*.dylib -o "$T/grow/m" 2>/dev/null
"${TOOL[@]}" dylib "$T/grow/m" "$T/grow/out.dylib" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "header padding" "$T/err" && [ ! -e "$T/grow/out.dylib" ]; } || { echo "FAIL: overflowing header not refused (rc $rc)"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_image_test: passed" || { echo "$fails failure(s)"; exit 1; }
