#!/bin/bash
# dylib --hooks: a hooked function jumps through the hook table; a host that fills the table
# from a dyld add-image callback (before initializers) runs its replacement instead.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL=(python3 -I "$ROOT/tools/launcher/mcfm_image.py")
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
fails=0
ADDR="$(nm "$T/fixture" | awk '$3=="_fixture_answer"{print "0x"$1}')"
[ -n "$ADDR" ] || { echo "FAIL: no _fixture_answer symbol"; exit 1; }
printf 'fixture_answer\t%s\n' "$ADDR" > "$T/hooks.tsv"
"${TOOL[@]}" imports "$T/fixture" > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T" >/dev/null
"${TOOL[@]}" dylib "$T/fixture" "$T/libminecraftpe.dylib" --hooks "$T/hooks.tsv" || { echo "FAIL: dylib --hooks exited $?"; exit 1; }
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null
cat > "$T/host.cpp" <<'EOF'
#include "hook_table.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstring>
static int replaced = 0;
static int replacement(int x) { replaced++; return x + 1000; }
static void on_add(const mach_header *h, intptr_t slide) {
  Dl_info info;
  if (!dladdr(h, &info) || !strstr(info.dli_fname, "libminecraftpe")) return;
  uintptr_t table = mcfm::hook_table_address(h);
  if (!table || mcfm::hook_table_capacity(h) < 1) return;
  reinterpret_cast<void **>(table + slide)[0] = reinterpret_cast<void *>(&replacement);
}
int main(int, char **argv) {
  _dyld_register_func_for_add_image(on_add);
  if (!dlopen(argv[1], RTLD_NOW)) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
  std::printf("replaced=%d\n", replaced);
  return replaced == 1 ? 0 : 2;
}
EOF
clang++ -arch arm64 -mmacosx-version-min=11.0 -std=c++11 -I "$ROOT/shared/apple" "$T/host.cpp" \
  "$ROOT/shared/apple/hook_table.cpp" -Wl,-rpath,@executable_path -o "$T/host" || { echo "FAIL: host build"; exit 1; }
OUT="$("$T/host" "$T/libminecraftpe.dylib" 2>&1)" || { echo "FAIL: hook not taken: $OUT"; fails=$((fails+1)); }
# Refusals: address outside __TEXT, too many hooks; nothing written.
printf 'bad\t0x10\n' > "$T/bad.tsv"
"${TOOL[@]}" dylib "$T/fixture" "$T/bad.dylib" --hooks "$T/bad.tsv" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "not in __TEXT" "$T/err" && [ ! -e "$T/bad.dylib" ]; } || { echo "FAIL: bad address not refused (rc $rc)"; fails=$((fails+1)); }
python3 -c "import sys; [print('h%d\t%s' % (i, sys.argv[1])) for i in range(100000)]" "$ADDR" > "$T/many.tsv"
"${TOOL[@]}" dylib "$T/fixture" "$T/many.dylib" --hooks "$T/many.tsv" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "hook table full" "$T/err" && [ ! -e "$T/many.dylib" ]; } || { echo "FAIL: overfull table not refused (rc $rc)"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_hooks_test: passed" || { echo "$fails failure(s)"; exit 1; }
