#!/bin/bash
# usage: build_stubs.sh <imports.tsv> <outdir>
# Builds libmcfm_stubrt.dylib and mcfm_stub_<lib>.dylib for every stubbed library listed
# in imports.tsv (mcfm_image.py imports). All install names are @rpath/….
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSV="$1"; OUT="$2"
CC=(clang -arch arm64 -mmacosx-version-min=11.0 -O1 -Wall -dynamiclib)
rm -rf "$OUT/src"; mkdir -p "$OUT/src"
"${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -install_name @rpath/libmcfm_stubrt.dylib \
  -o "$OUT/libmcfm_stubrt.dylib"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" stubs "$TSV" "$OUT/src"
for c in "$OUT"/src/*.c; do
  [ -e "$c" ] || continue
  lib="$(basename "$c" .c)"
  "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" "$OUT/libmcfm_stubrt.dylib" \
    -install_name "@rpath/mcfm_stub_$lib.dylib" -o "$OUT/mcfm_stub_$lib.dylib"
done
