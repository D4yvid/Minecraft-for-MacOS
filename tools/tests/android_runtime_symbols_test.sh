#!/bin/bash
# usage: android_runtime_symbols_test.sh <libmcfm_runtime.so> <libmcfm_launcher.so> <llvm-nm>
# The Apple-ABI runtime defines every libc++ symbol the game imports (in its generated table,
# runtime_symbols.txt next to libmcfm_runtime.so; required list:
# android/launcher/runtime/required_libcxx.txt, plus dist/launcher/imports.tsv when present), and
# the launcher library exports none of the runtime's C++ symbols: Android's system libc++ has the
# same std::__1 names with another ABI, and system libraries must keep binding to it.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RT="$1"; LAUNCHER="$2"; NM="$3"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
TABLE="$(dirname "$RT")/runtime_symbols.txt"
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
LEAKED="$(grep -E '^(_Z|__cxa_|_Unwind_|__gxx_|__unw_)' <<<"$EXPORTS")"
[ -z "$LEAKED" ] || { echo "FAIL: libmcfm_launcher.so exports C++ runtime symbols, e.g.:"; head -5 <<<"$LEAKED" | sed 's/^/  /'; fails=$((fails+1)); }
[ $fails = 0 ] && echo "android_runtime_symbols_test: passed ($(wc -l < "$T/need" | tr -d ' ') required, launcher exports: $(tr '\n' ' ' <<<"$EXPORTS"))" || { echo "$fails failure(s)"; exit 1; }
