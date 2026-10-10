#!/bin/bash
# usage: android_launcher_test.sh <android launcher out dir>   (make android-test)
# Mach-O fixtures built on the Mac, loaded on the device by mcfm-run with our loader, the Darwin
# layer and the Apple-ABI runtime (docs/LAUNCHER.md, Stage 3a). Needs a running emulator/device.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"
RUN=(bash "$ROOT/tools/android/adb_run.sh")
ANDROID_CC="${ANDROID_CC:-$HOME/Library/Android/sdk/ndk/27.3.13750724/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
expect() {  # expect <output> <line>
  grep -qxF "$2" <<<"$1" || { echo "FAIL: expected '$2' in:"; sed 's/^/  | /' <<<"$1"; fails=$((fails+1)); }
}

# 1. A C image: libSystem binds to bionic, an initializer runs, an exported function is called.
cat > "$T/fx.c" <<'EOF'
#include <string.h>
#include <unistd.h>
static int inited;
// getpid(): an external call, so clang cannot evaluate the constructor at compile time.
__attribute__((constructor)) static void init(void) { inited = getpid() > 0 ? 5 : 100; }
int fx_check(int unused) { (void)unused; return (int)strlen("hello") + inited; }
EOF
clang -arch arm64 -dynamiclib -O1 -mmacosx-version-min=11.0 -Wl,-no_fixup_chains "$T/fx.c" -o "$T/fx.dylib" \
  || { echo "FAIL: build fx.dylib"; exit 1; }
OUTPUT="$("${RUN[@]}" --push "$OUT/libmcfm_launcher.so" --push "$T/fx.dylib" "$OUT/mcfm-run" @DIR@/fx.dylib \
  --call fx_check 2>&1)" || { echo "FAIL: mcfm-run fx.dylib exited $?"; fails=$((fails+1)); }
expect "$OUTPUT" "mcfm: 1 initializers ran"
expect "$OUTPUT" "fx_check=10"

# 2. The launcher's loader fixture (C++): an exception thrown 5 frames deep and caught inside the
#    image (compact unwind through the runtime's libunwind), an initializer that throws, catches
#    and calls the hooked function with argc, a FakeKit stub, operator new coalesced to ours.
bash "$ROOT/tools/tests/loader_fixture.sh" "$T/lf" >/dev/null || { echo "FAIL: loader fixture"; exit 1; }
ANDROID_CC="$ANDROID_CC" bash "$ROOT/tools/launcher/build_stubs.sh" --target android "$T/lf/imports.tsv" "$T/lf/android" >/dev/null \
  || { echo "FAIL: android stubs"; exit 1; }
HOOK="$(awk '$1=="_fixture_answer"{print $2}' "$T/lf/symbols.txt")"
ARGS=(@DIR@/libminecraftpe.dylib --hook "$HOOK" --call fixture_thrower --call fixture_answer=5 --int fixture_init_answer --weak _Znwm)
OUTPUT="$("${RUN[@]}" --push "$OUT/libmcfm_launcher.so" --push "$T/lf/libminecraftpe.dylib" \
  --push "$T/lf/android/libmcfm_stubrt.so" --push "$T/lf/android/mcfm_stub_FakeKit.so" "$OUT/mcfm-run" "${ARGS[@]}" 2>&1)" \
  || { echo "FAIL: mcfm-run loader fixture exited $?"; fails=$((fails+1)); }
expect "$OUTPUT" "mcfm: 1 initializers ran"
expect "$OUTPUT" "fixture_thrower=41"
expect "$OUTPUT" "fixture_answer=1005"
expect "$OUTPUT" "fixture_init_answer=$((1000 + 1 + ${#ARGS[@]}))"  # the initializer got mcfm-run's argc
expect "$OUTPUT" "weak _Znwm=runtime"
# 4. GLES through the OpenGLES framework (Stage 3b): resolved to the system's GLES, OES entry
#    points included, with a GL context from mcfm-run --gl; EAGL constants stay stubbed.
bash "$ROOT/tools/tests/gl_fixture.sh" "$T/gl" >/dev/null || { echo "FAIL: gl fixture"; exit 1; }
ANDROID_CC="$ANDROID_CC" bash "$ROOT/tools/launcher/build_stubs.sh" --target android "$T/gl/imports.tsv" "$T/gl/android" >/dev/null \
  || { echo "FAIL: gl stubs"; exit 1; }
OUTPUT="$("${RUN[@]}" --push "$OUT/libmcfm_launcher.so" --push "$T/gl/libminecraftpe.dylib" \
  --push "$T/gl/android/libmcfm_stubrt.so" --push "$T/gl/android/mcfm_stub_OpenGLES.so" "$OUT/mcfm-run" \
  @DIR@/libminecraftpe.dylib --gl --call gl_check 2>&1)" || { echo "FAIL: mcfm-run gl fixture exited $?"; fails=$((fails+1)); }
expect "$OUTPUT" "gl_check=$((0x4080bf))"
if grep -q "^mcfm: stub OpenGLES:_gl" <<<"$OUTPUT"; then echo "FAIL: a gl* call reached a stub:"; grep "stub OpenGLES" <<<"$OUTPUT"; fails=$((fails+1)); fi

# 3. The Darwin conformance fixture: the transcript on Android equals the one on the Mac.
clang++ -arch arm64 -std=c++17 -O1 -dynamiclib -mmacosx-version-min=11.0 -Wl,-no_fixup_chains \
  "$ROOT/tools/tests/darwin_conformance.cpp" -o "$T/conformance.dylib" || { echo "FAIL: build conformance"; exit 1; }
clang -arch arm64 "$ROOT/tools/tests/conformance_host.c" -o "$T/conformance_host" || { echo "FAIL: build host"; exit 1; }
mkdir -p "$T/mac" && (cd "$T/mac" && ../conformance_host ../conformance.dylib < /dev/null > ../mac.txt 2>&1) \
  || { echo "FAIL: conformance on the Mac"; cat "$T/mac.txt"; exit 1; }
"${RUN[@]}" --push "$OUT/libmcfm_launcher.so" --push "$T/conformance.dylib" "$OUT/mcfm-run" \
  @DIR@/conformance.dylib --call conformance_main=0 < /dev/null > "$T/android.raw" 2>&1 \
  || { echo "FAIL: conformance on Android exited $?"; sed 's/^/  | /' "$T/android.raw"; fails=$((fails+1)); }
grep -v -e '^mcfm: ' -e '^conformance_main=' "$T/android.raw" > "$T/android.txt"
if ! diff -u "$T/mac.txt" "$T/android.txt" > "$T/diff.txt"; then
  echo "FAIL: conformance transcript differs (--- Mac, +++ Android):"; sed 's/^/  /' "$T/diff.txt"; fails=$((fails+1))
fi
[ $fails = 0 ] && echo "android_launcher_test: passed" || { echo "$fails failure(s)"; exit 1; }
