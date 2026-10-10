#!/bin/bash
# A provider library replaces stubs: its exports win (re-exported), the rest stays stubbed.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T/stubs"
printf 'int fakekit_hello(void) { return 7; }\n' > "$T/prov.c"
clang -arch arm64 -mmacosx-version-min=11.0 -dynamiclib "$T/prov.c" -install_name @rpath/libprov.dylib -o "$T/stubs/libprov.dylib"
printf 'FakeKit\t-\tlib\nFakeKit\t_fakekit_hello\tfn\nFakeKit\t_fakekit_other\tfn\n' > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T/stubs" --provider "FakeKit=$T/stubs/libprov.dylib" \
  || { echo "FAIL: build_stubs --provider"; exit 1; }
fails=0
grep -q "_fakekit_hello" "$T/stubs/src/FakeKit.c" && { echo "FAIL: provided symbol still stubbed"; fails=$((fails+1)); }
grep -q "_fakekit_other" "$T/stubs/src/FakeKit.c" || { echo "FAIL: missing stub for unprovided symbol"; fails=$((fails+1)); }
# Runtime check (what dyld does for the game image): ld cannot resolve an @rpath re-export
# when linking a test program, so look the symbols up through the stub with dlopen/dlsym.
cat > "$T/use.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
  void *h = dlopen(argv[1], RTLD_NOW);
  if (!h) { puts(dlerror()); return 1; }
  int (*hello)(void) = (int (*)(void))dlsym(h, "fakekit_hello");
  int (*other)(void) = (int (*)(void))dlsym(h, "fakekit_other");
  if (!hello || !other) { puts("symbol missing"); return 1; }
  return hello() == 7 && other() == 0 ? 0 : 2;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/use.c" -Wl,-rpath,"$T/stubs" -o "$T/use" || { echo "FAIL: build use"; exit 1; }
OUT="$("$T/use" "$T/stubs/mcfm_stub_FakeKit.dylib" 2>&1)" || { echo "FAIL: provider not re-exported or stub not 0: $OUT"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_provider_test: passed" || { echo "$fails failure(s)"; exit 1; }
