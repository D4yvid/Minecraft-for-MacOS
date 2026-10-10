#!/bin/bash
# usage: build_stubs.sh [--target android] <imports.tsv> <outdir> [--provider <lib>=<dylib>]...
# Builds libmcfm_stubrt.dylib and mcfm_stub_<lib>.dylib for every stubbed library listed
# in imports.tsv (mcfm_image.py imports). A provider's exports are not stubbed: the stub
# re-exports the provider (e.g. OpenGLES=libGLESv2.dylib from ANGLE). Install names @rpath/….
# --target android (Stage 3): ELF libmcfm_stub_<lib>.so files for arm64 (soname = file name,
# 16 KB pages; the lib prefix the APK installer needs) built with $ANDROID_CC; symbols keep their
# Mach-O names (leading '_'). No providers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET=macos
if [ "${1:-}" = "--target" ]; then TARGET="$2"; shift 2; fi
TSV="$1"; OUT="$2"; shift 2
CC=(clang -arch arm64 -mmacosx-version-min=11.0 -O1 -Wall -dynamiclib)
EXT=dylib
if [ "$TARGET" = android ]; then
  [ -x "${ANDROID_CC:-}" ] || { echo "build_stubs.sh: set ANDROID_CC to the NDK's aarch64-linux-android28-clang" >&2; exit 2; }
  CC=("$ANDROID_CC" -O1 -Wall -fPIC -shared -Wl,-z,max-page-size=16384)
  EXT=so
elif [ "$TARGET" != macos ]; then
  echo "build_stubs.sh: unknown target $TARGET" >&2; exit 2
fi
rm -rf "$OUT/src"; mkdir -p "$OUT/src"
PROVIDED=(); declare -a PROVIDER_LIBS=(); declare -a PROVIDER_DYLIBS=()
while [ $# -gt 0 ]; do
  [ "$1" = "--provider" ] && [ $# -ge 2 ] || { echo "build_stubs.sh: bad argument $1" >&2; exit 2; }
  [ "$TARGET" = macos ] || { echo "build_stubs.sh: providers are macOS-only for now" >&2; exit 2; }
  lib="${2%%=*}"; dylib="${2#*=}"
  nm -gUj "$dylib" > "$OUT/src/$lib.provided"
  PROVIDED+=(--provided "$lib" "$OUT/src/$lib.provided")
  PROVIDER_LIBS+=("$lib"); PROVIDER_DYLIBS+=("$dylib")
  shift 2
done
if [ "$TARGET" = android ]; then
  "${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -Wl,-soname,libmcfm_stubrt.so -o "$OUT/libmcfm_stubrt.so"
else
  "${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -install_name @rpath/libmcfm_stubrt.dylib \
    -o "$OUT/libmcfm_stubrt.dylib"
fi
python3 -I "$ROOT/tools/launcher/mcfm_image.py" stubs "$TSV" "$OUT/src" ${PROVIDED[@]+"${PROVIDED[@]}"}
for c in "$OUT"/src/*.c; do
  [ -e "$c" ] || continue
  lib="$(basename "$c" .c)"
  extra=()
  for i in ${PROVIDER_LIBS[@]+"${!PROVIDER_LIBS[@]}"}; do
    [ "${PROVIDER_LIBS[$i]}" = "$lib" ] && extra=(-Wl,-reexport_library,"${PROVIDER_DYLIBS[$i]}")
  done
  if [ "$TARGET" = android ]; then
    "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" -L"$OUT" -lmcfm_stubrt \
      -Wl,-soname,"libmcfm_stub_$lib.so" -o "$OUT/libmcfm_stub_$lib.so"
  else
    "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" "$OUT/libmcfm_stubrt.dylib" \
      ${extra[@]+"${extra[@]}"} -install_name "@rpath/mcfm_stub_$lib.dylib" -o "$OUT/mcfm_stub_$lib.dylib"
  fi
done
