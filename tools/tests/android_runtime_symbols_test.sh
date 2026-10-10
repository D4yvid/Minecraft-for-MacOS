#!/bin/bash
# usage: android_runtime_symbols_test.sh <runtime out dir> <libmcfm_launcher.so> <llvm-nm> [test executables...]
# The Apple-ABI runtime defines every libc++ symbol the game imports (in its generated table,
# <runtime out dir>/runtime_symbols.txt; required list:
# android/launcher/runtime/required_libcxx.txt, plus dist/launcher/imports.tsv when present), and
# no binary exports the runtime's C++ symbols (the launcher library, the test executables):
# Android's system libc++ has the same std::__1 names with another ABI, and system libraries
# loaded in the same process must keep binding to it.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RT_DIR="$1"; LAUNCHER="$2"; NM="$3"; shift 3
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
TABLE="$RT_DIR/runtime_symbols.txt"
[ -s "$TABLE" ] || { echo "FAIL: no runtime symbol table at $TABLE"; exit 1; }
grep -v '^#' "$ROOT/android/launcher/runtime/required_libcxx.txt" > "$T/need"
if [ -f "$ROOT/dist/launcher/imports.tsv" ]; then
  awk -F'\t' '$1=="libc++" && $2!="-" {n=$2; sub(/^_/,"",n); print n}' "$ROOT/dist/launcher/imports.tsv" >> "$T/need"
fi
sort -u -o "$T/need" "$T/need"
sort -u "$TABLE" > "$T/have"
MISSING="$(comm -23 "$T/need" "$T/have")"
[ -z "$MISSING" ] || { echo "FAIL: the runtime does not define:"; sed 's/^/  /' <<<"$MISSING"; fails=$((fails+1)); }
EXPORTS="$("$NM" -D --defined-only "$LAUNCHER" | awk '{print $3}')"
# ... nor needs one from elsewhere (an undefined C++ symbol would bind to Android's libc++);
# bionic's own __cxa_atexit/__cxa_finalize/__cxa_thread_atexit_impl are expected.
UNDEF="$("$NM" -D -u "$LAUNCHER" | awk '{print $NF}' | sed 's/@.*//' | grep -E '^(_Z|__cxa_|_Unwind_|__gxx_)' \
  | grep -v -x -e __cxa_atexit -e __cxa_finalize -e __cxa_thread_atexit_impl)"
[ -z "$UNDEF" ] || { echo "FAIL: libmcfm_launcher.so needs C++ runtime symbols from elsewhere:"; head -5 <<<"$UNDEF" | sed 's/^/  /'; fails=$((fails+1)); }
for bin in "$LAUNCHER" "$@"; do
  LEAKED="$("$NM" -D --defined-only "$bin" | awk '{print $3}' | grep -E '^(_Z|__cxa_|_Unwind_|__gxx_|__unw_)')"
  [ -z "$LEAKED" ] || { echo "FAIL: $(basename "$bin") exports C++ runtime symbols, e.g.:"; head -5 <<<"$LEAKED" | sed 's/^/  /'; fails=$((fails+1)); }
done
[ $fails = 0 ] && echo "android_runtime_symbols_test: passed ($(wc -l < "$T/need" | tr -d ' ') required, launcher exports: $(tr '\n' ' ' <<<"$EXPORTS"))" || { echo "$fails failure(s)"; exit 1; }
