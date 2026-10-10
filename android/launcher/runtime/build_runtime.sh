#!/bin/bash
# usage: build_runtime.sh <llvm runtimes dir> <NDK clang bin dir> <outdir>
# Builds libmcfm_runtime.so for the Android launcher (docs/LAUNCHER.md, Stage 3a): LLVM 18.1.8
# libc++, libc++abi and libunwind with Apple's arm64 ABI settings (runtime/include) plus the
# Darwin libSystem layer (android/launcher/darwin). Writes:
#   <outdir>/libmcfm_runtime.so
#   <outdir>/include/        __config_site, __external_threading, __assertion_handler
#   <outdir>/src/            the patched LLVM sources (their include/ dirs are the headers)
# Code built against this runtime uses: $(runtime_cxxflags) below, printed by --print-cxxflags.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
LLVM="$1"; BIN="$2"; OUT="$3"
CC="$BIN/aarch64-linux-android28-clang"
CXX="$BIN/aarch64-linux-android28-clang++"
SRC="$OUT/src"; OBJ="$OUT/obj"; INC="$OUT/include"
[ -d "$LLVM/libcxx" ] && [ -d "$LLVM/libcxxabi" ] && [ -d "$LLVM/libunwind" ] \
  || { echo "build_runtime: no LLVM sources in $LLVM (make llvm-runtimes)" >&2; exit 1; }
[ -x "$CXX" ] || { echo "build_runtime: no NDK compiler at $CXX (make android-sdk)" >&2; exit 1; }

# 1. A patched copy of the sources (the fetched tree stays pristine).
rm -rf "$SRC" "$OBJ"
mkdir -p "$SRC" "$OBJ" "$INC"
cp -R "$LLVM/libcxx" "$LLVM/libcxxabi" "$LLVM/libunwind" "$SRC/"
python3 -I "$ROOT/android/launcher/runtime/patch_llvm.py" "$SRC" >/dev/null
cp "$ROOT/android/launcher/runtime/include/__config_site" "$ROOT/android/launcher/runtime/include/__external_threading" "$INC/"
cp "$SRC/libcxx/vendor/llvm/default_assertion_handler.in" "$INC/__assertion_handler"

COMMON=(-fPIC -O2 -g -fsigned-char -funwind-tables -ffunction-sections -fdata-sections -Wno-unused-command-line-argument)
CXXINC=(-nostdinc++ -isystem "$INC" -isystem "$SRC/libcxx/include" -isystem "$SRC/libcxxabi/include" -I "$ROOT/android/launcher/darwin")

LIBCXX=(algorithm any bind call_once charconv chrono error_category exception filesystem/filesystem_clock
  filesystem/filesystem_error filesystem/path functional hash legacy_pointer_safety memory memory_resource
  new_handler new_helpers optional print random_shuffle ryu/d2fixed ryu/d2s ryu/f2s stdexcept string
  system_error typeinfo valarray variant vector verbose_abort atomic barrier condition_variable_destructor
  condition_variable future mutex_destructor mutex shared_mutex thread random fstream ios ios.instantiations
  iostream locale ostream regex strstream filesystem/directory_entry filesystem/directory_iterator
  filesystem/operations)
LIBCXXABI=(cxa_aux_runtime cxa_default_handlers cxa_demangle cxa_exception_storage cxa_guard cxa_handlers
  cxa_vector cxa_virtual stdlib_exception stdlib_stdexcept stdlib_typeinfo abort_message fallback_malloc
  private_typeinfo stdlib_new_delete cxa_exception cxa_personality cxa_thread_atexit)
DARWIN=(pthread errno ctype symbols files misc system mach blocks dispatch net locale crypto)
DARWIN_C=(stdio)

jobs=()
compile() {  # compile <object> <compiler and flags...>
  local o="$1"; shift
  mkdir -p "$(dirname "$o")"
  "$@" -c -o "$o" &
  jobs+=($!)
  if [ ${#jobs[@]} -ge 8 ]; then wait "${jobs[0]}"; jobs=("${jobs[@]:1}"); fi
}
for f in "${LIBCXX[@]}"; do
  compile "$OBJ/libcxx/$f.o" "$CXX" "${COMMON[@]}" "${CXXINC[@]}" -std=c++23 -D_LIBCPP_BUILDING_LIBRARY \
    -DLIBCXX_BUILDING_LIBCXXABI -fvisibility-inlines-hidden -I "$SRC/libcxx/src" "$SRC/libcxx/src/$f.cpp"
done
for f in "${LIBCXXABI[@]}"; do
  compile "$OBJ/libcxxabi/$f.o" "$CXX" "${COMMON[@]}" "${CXXINC[@]}" -std=c++23 -D_LIBCXXABI_BUILDING_LIBRARY \
    -D_LIBCPP_BUILDING_LIBRARY -DHAVE___CXA_THREAD_ATEXIT_IMPL -I "$SRC/libcxxabi/src" -I "$SRC/libcxx/src" \
    "$SRC/libcxxabi/src/$f.cpp"
done
# -Wno-format: upstream printf formats in the (Apple-only until now) compact-unwind logging.
UNW=(-D_LIBUNWIND_MCFM_DYNAMIC_SECTIONS -D_LIBUNWIND_IS_NATIVE_ONLY -Wno-format -I "$SRC/libunwind/include")
compile "$OBJ/libunwind/libunwind.o" "$CXX" "${COMMON[@]}" "${UNW[@]}" -std=c++17 -nostdinc++ -fno-exceptions -fno-rtti \
  "$SRC/libunwind/src/libunwind.cpp"
for f in UnwindLevel1 UnwindLevel1-gcc-ext; do
  compile "$OBJ/libunwind/$f.o" "$CC" "${COMMON[@]}" "${UNW[@]}" -std=c99 "$SRC/libunwind/src/$f.c"
done
for f in UnwindRegistersRestore UnwindRegistersSave; do
  compile "$OBJ/libunwind/$f.o" "$CC" "${COMMON[@]}" "${UNW[@]}" "$SRC/libunwind/src/$f.S"
done
for f in "${DARWIN[@]}"; do
  compile "$OBJ/darwin/$f.o" "$CXX" "${COMMON[@]}" "${CXXINC[@]}" -std=c++17 -Wall -Wextra \
    "$ROOT/android/launcher/darwin/$f.cpp"
done
for f in "${DARWIN_C[@]}"; do
  compile "$OBJ/darwin/$f.o" "$CC" "${COMMON[@]}" -std=c11 -Wall -Wextra "$ROOT/android/launcher/darwin/$f.c"
done
fail=0
for j in "${jobs[@]}"; do wait "$j" || fail=1; done
[ $fail = 0 ] || { echo "build_runtime: compilation failed" >&2; exit 1; }

"$CXX" -shared -nostdlib++ --unwindlib=none -Wl,-soname,libmcfm_runtime.so -Wl,-z,max-page-size=16384 \
  -Wl,--gc-sections $(find "$OBJ" -name '*.o' | sort) -ldl -o "$OUT/libmcfm_runtime.so"
echo "build_runtime: $OUT/libmcfm_runtime.so"
