#!/bin/bash
# The Darwin layer's libSystem table (android/launcher/darwin/symbols.cpp) lists every libSystem
# symbol the game imports (required_libsystem.txt; also dist/launcher/imports.tsv when present),
# sorted by name (the lookup is a binary search) and without duplicates.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
sed -n 's/^    [SBR]("\([^"]*\)".*/\1/p' "$ROOT/android/launcher/darwin/symbols.cpp" > "$T/table"
[ -s "$T/table" ] || { echo "FAIL: no entries found in symbols.cpp"; exit 1; }
LC_ALL=C sort -c "$T/table" 2>/dev/null || { echo "FAIL: symbols.cpp table not sorted: $(LC_ALL=C sort -c "$T/table" 2>&1)"; fails=$((fails+1)); }
DUP="$(sort "$T/table" | uniq -d)"
[ -z "$DUP" ] || { echo "FAIL: duplicate entries: $DUP"; fails=$((fails+1)); }
grep -v '^#' "$ROOT/android/launcher/darwin/required_libsystem.txt" > "$T/need"
if [ -f "$ROOT/dist/launcher/imports.tsv" ]; then
  awk -F'\t' '$1=="libSystem" && $2!="-" && $2!="dyld_stub_binder" {n=$2; sub(/^_/,"",n); print n}' "$ROOT/dist/launcher/imports.tsv" >> "$T/need"
fi
sort -u -o "$T/need" "$T/need"
MISSING="$(comm -23 "$T/need" <(sort -u "$T/table"))"
[ -z "$MISSING" ] || { echo "FAIL: $(wc -l <<<"$MISSING" | tr -d ' ') libSystem imports not in symbols.cpp:"; tr '\n' ' ' <<<"$MISSING"; echo; fails=$((fails+1)); }
[ $fails = 0 ] && echo "android_symbols_test: passed ($(wc -l < "$T/table" | tr -d ' ') entries)" || { echo "$fails failure(s)"; exit 1; }
