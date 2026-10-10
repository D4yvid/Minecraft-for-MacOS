#!/bin/bash
# usage: loader_fixture.sh <outdir>
# A converted image exercising the loader: lazy and GOT binds, a data import with an addend, a
# weak definition libc++ also exports (operator new), an initializer, a C++ exception thrown and caught inside the image, and a hookable function.
# Builds <outdir>/fixture (iOS-tagged executable), <outdir>/libminecraftpe.dylib (converted,
# signed, hooked: fixture_answer), stubs, and <outdir>/symbols.txt (unslid addresses).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"; mkdir -p "$OUT"
CC=(clang++ -arch arm64 -mmacosx-version-min=11.0 -std=c++17)
cat > "$OUT/fakekit.cpp" <<'CPP'
extern "C" int fakekit_hello(void) { return 1; }
extern "C" { char kFakeKitValue[16] = "0123456789abcde"; }
CPP
"${CC[@]}" -dynamiclib "$OUT/fakekit.cpp" -install_name /System/Library/Frameworks/FakeKit.framework/FakeKit -o "$OUT/fakekit.dylib"
cat > "$OUT/fixture.cpp" <<'CPP'
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
extern "C" int fakekit_hello(void);
extern "C" char kFakeKitValue[16];
// A weak operator new: libc++ exports one too, so (as under dyld) libc++'s wins.
__attribute__((weak)) void *operator new(std::size_t n) { void *p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
static int initialized = 0;
__attribute__((constructor)) static void fixture_init() { initialized = 7 + fakekit_hello() - 1; }
__attribute__((noinline)) static void deep(int n) { if (n == 0) throw std::runtime_error("boom"); std::string s(40, 'x'); deep(n - 1); }
extern "C" __attribute__((noinline, used)) int fixture_thrower(void) {
  try { deep(5); } catch (const std::runtime_error &e) { return 35 + initialized + (std::string(e.what()) == "boom" ? 0 : 100); }
  return -1;
}
extern "C" __attribute__((noinline, used)) int fixture_answer(int x) { return x * 3 + 41; }  // hooked
extern "C" __attribute__((used)) const char *fixture_data_ptr = &kFakeKitValue[8];          // addend 8
int main() { return fixture_thrower(); }
CPP
"${CC[@]}" -O1 -Wl,-no_fixup_chains -Wl,-headerpad,0x1000 "$OUT/fixture.cpp" "$OUT/fakekit.dylib" -o "$OUT/fixture.mac"
vtool -set-build-version ios 15.0 15.0 -replace -output "$OUT/fixture" "$OUT/fixture.mac"
rm -f "$OUT/fixture.mac"
nm "$OUT/fixture" | awk '$3 ~ /^_fixture_|^_main$/ {print $3, "0x"$1}' > "$OUT/symbols.txt"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$OUT/fixture" > "$OUT/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$OUT/imports.tsv" "$OUT" >/dev/null
printf 'fixture_answer\t%s\n' "$(awk '$1=="_fixture_answer"{print $2}' "$OUT/symbols.txt")" > "$OUT/hooks.tsv"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$OUT/fixture" "$OUT/libminecraftpe.dylib" --hooks "$OUT/hooks.tsv"
codesign -f -s - "$OUT/libminecraftpe.dylib" 2>/dev/null
