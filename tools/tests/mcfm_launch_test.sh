#!/bin/bash
# mcfm-launch: refuses a non-game image before loading it (no foreign initializer runs)
# and reports unreadable paths. The real game is covered by make launcher-check.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
make -s build/launcher/mcfm-launch >/dev/null || { echo "FAIL: build mcfm-launch"; exit 1; }
BIN="$ROOT/build/launcher/mcfm-launch"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash tools/tests/launcher_fixture.sh "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
python3 -I tools/launcher/mcfm_image.py imports "$T/fixture" > "$T/imports.tsv"
bash tools/launcher/build_stubs.sh "$T/imports.tsv" "$T" >/dev/null
python3 -I tools/launcher/mcfm_image.py dylib "$T/fixture" "$T/libminecraftpe.dylib"
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null || { echo "FAIL: codesign"; exit 1; }
fails=0
OUT="$(MCFM_CENSUS="$T/census.txt" "$BIN" "$T/libminecraftpe.dylib" 2>&1)"; rc=$?
[ $rc = 3 ] || { echo "FAIL: fixture image: exit $rc, want 3: $OUT"; fails=$((fails+1)); }
grep -q "is not Minecraft PE 0.15.10" <<<"$OUT" || { echo "FAIL: no UUID message: $OUT"; fails=$((fails+1)); }
[ -e "$T/census.txt" ] && { echo "FAIL: fixture initializers ran before the UUID check"; fails=$((fails+1)); }
OUT="$("$BIN" "$T/missing.dylib" 2>&1)"; rc=$?
{ [ $rc = 2 ] && grep -q "cannot load" <<<"$OUT"; } || { echo "FAIL: missing file: exit $rc: $OUT"; fails=$((fails+1)); }
# A crafted header claiming huge load commands: refused (exit 3), not a crash.
python3 -c "import struct,sys; open(sys.argv[1],'wb').write(struct.pack('<IiiIIIII',0xFEEDFACF,0x0100000C,0,6,1000,0x10000000,0,0)+struct.pack('<II',0x19,0x01000000)+bytes(100))" "$T/crafted.dylib"
OUT="$("$BIN" "$T/crafted.dylib" 2>&1)"; rc=$?
[ $rc = 3 ] || { echo "FAIL: crafted header: exit $rc, want 3: $OUT"; fails=$((fails+1)); }
# Bad command lines are refused before anything is loaded (exit 2, usage).
usage() {  # <description> <args...>
  local what="$1"; shift
  OUT="$("$BIN" "$@" 2>&1)"; rc=$?
  { [ $rc = 2 ] && grep -q "usage:" <<<"$OUT"; } || { echo "FAIL: $what: exit $rc, want 2 with usage: $OUT"; fails=$((fails+1)); }
}
usage "--frames 0" --frames 0 "$T/libminecraftpe.dylib"
usage "--frames abc" --frames abc "$T/libminecraftpe.dylib"
usage "--frames without a value" "$T/libminecraftpe.dylib" --frames
usage "--screenshot without --frames" --screenshot "$T/x.ppm" "$T/libminecraftpe.dylib"
usage "unknown option" --bogus "$T/libminecraftpe.dylib"
[ $fails = 0 ] && echo "mcfm_launch_test: passed" || { echo "$fails failure(s)"; exit 1; }
