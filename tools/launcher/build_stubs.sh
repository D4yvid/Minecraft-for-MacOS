#!/bin/bash
# usage: build_stubs.sh <imports.tsv> <outdir> [--provider <lib>=<dylib>]...
# Builds libmcfm_stubrt.dylib and mcfm_stub_<lib>.dylib for every stubbed library listed
# in imports.tsv (mcfm_image.py imports). A provider's exports are not stubbed: the stub
# re-exports the provider (e.g. OpenGLES=libGLESv2.dylib from ANGLE). Install names @rpath/….
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSV="$1"; OUT="$2"; shift 2
CC=(clang -arch arm64 -mmacosx-version-min=11.0 -O1 -Wall -dynamiclib)
rm -rf "$OUT/src"; mkdir -p "$OUT/src"
PROVIDED=(); declare -a PROVIDER_LIBS=(); declare -a PROVIDER_DYLIBS=()
while [ $# -gt 0 ]; do
  [ "$1" = "--provider" ] && [ $# -ge 2 ] || { echo "build_stubs.sh: bad argument $1" >&2; exit 2; }
  lib="${2%%=*}"; dylib="${2#*=}"
  nm -gUj "$dylib" > "$OUT/src/$lib.provided"
  PROVIDED+=(--provided "$lib" "$OUT/src/$lib.provided")
  PROVIDER_LIBS+=("$lib"); PROVIDER_DYLIBS+=("$dylib")
  shift 2
done
"${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -install_name @rpath/libmcfm_stubrt.dylib \
  -o "$OUT/libmcfm_stubrt.dylib"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" stubs "$TSV" "$OUT/src" ${PROVIDED[@]+"${PROVIDED[@]}"}
for c in "$OUT"/src/*.c; do
  [ -e "$c" ] || continue
  lib="$(basename "$c" .c)"
  extra=()
  for i in ${PROVIDER_LIBS[@]+"${!PROVIDER_LIBS[@]}"}; do
    [ "${PROVIDER_LIBS[$i]}" = "$lib" ] && extra=(-Wl,-reexport_library,"${PROVIDER_DYLIBS[$i]}")
  done
  "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" "$OUT/libmcfm_stubrt.dylib" \
    ${extra[@]+"${extra[@]}"} -install_name "@rpath/mcfm_stub_$lib.dylib" -o "$OUT/mcfm_stub_$lib.dylib"
done
