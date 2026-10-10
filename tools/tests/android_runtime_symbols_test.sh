#!/bin/bash
# usage: android_runtime_symbols_test.sh <libmcfm_runtime.so> <llvm-nm>
# The Apple-ABI runtime exports every libc++ symbol the game imports
# (android/launcher/runtime/required_libcxx.txt; also dist/launcher/imports.tsv when present).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SO="$1"; NM="$2"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$NM" -D --defined-only "$SO" | awk '{print $3}' | sort -u > "$T/have" || { echo "FAIL: cannot read $SO"; exit 1; }
grep -v '^#' "$ROOT/android/launcher/runtime/required_libcxx.txt" | sort -u > "$T/need"
if [ -f "$ROOT/dist/launcher/imports.tsv" ]; then
  awk -F'\t' '$1=="libc++" && $2!="-" {n=$2; sub(/^_/,"",n); print n}' "$ROOT/dist/launcher/imports.tsv" >> "$T/need"
  sort -u -o "$T/need" "$T/need"
fi
MISSING="$(comm -23 "$T/need" "$T/have")"
[ -z "$MISSING" ] || { echo "FAIL: libmcfm_runtime.so does not export:"; sed 's/^/  /' <<<"$MISSING"; exit 1; }
echo "android_runtime_symbols_test: passed ($(wc -l < "$T/need" | tr -d ' ') symbols)"
