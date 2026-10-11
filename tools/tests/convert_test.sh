#!/bin/bash
# The C++ converter (shared/loader/convert.cpp, used on Android to import the IPA) produces the
# same bytes as mcfm_image.py dylib --hooks (after thinning a FAT binary), and refuses what it
# refuses. On the real game too when game-files/ has it (make test without game files skips it).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
TOOL="$ROOT/build/test/convert_tool"
make -s -C "$ROOT" build/test/convert_tool >/dev/null || { echo "FAIL: build convert_tool"; exit 1; }
bash "$ROOT/tools/tests/loader_fixture.sh" "$T/f" >/dev/null || { echo "FAIL: loader fixture"; exit 1; }
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$T/f/fixture" "$T/py.dylib" --hooks "$T/f/hooks.tsv"
"$TOOL" "$T/f/fixture" "$T/f/hooks.tsv" "$T/cc.dylib" || { echo "FAIL: convert_tool refused the fixture"; fails=$((fails+1)); }
cmp -s "$T/py.dylib" "$T/cc.dylib" || { echo "FAIL: fixture: C++ output differs from mcfm_image.py"; fails=$((fails+1)); }
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$T/f/fixture" "$T/py-nohooks.dylib"
"$TOOL" "$T/f/fixture" /dev/null "$T/cc-nohooks.dylib" && cmp -s "$T/py-nohooks.dylib" "$T/cc-nohooks.dylib" \
  || { echo "FAIL: fixture without hooks differs"; fails=$((fails+1)); }

# The import the Android app runs (shared/launcher/game_import.cpp) on the same fixture.
make -s -C "$ROOT" build/test/game_import_test >/dev/null || { echo "FAIL: build game_import_test"; exit 1; }
mkdir -p "$T/import"
"$ROOT/build/test/game_import_test" "$T/f/fixture" "$T/f/hooks.tsv" "$T/py.dylib" "$T/import" || fails=$((fails+1))

# A FAT binary: the arm64 slice is converted.
echo 'int main(void) { return 0; }' > "$T/x.c"
clang -arch x86_64 -mmacosx-version-min=11.0 "$T/x.c" -o "$T/x86"
lipo -create "$T/f/fixture" "$T/x86" -output "$T/fat"
"$TOOL" "$T/fat" "$T/f/hooks.tsv" "$T/cc-fat.dylib" && cmp -s "$T/py.dylib" "$T/cc-fat.dylib" \
  || { echo "FAIL: FAT binary not converted like its arm64 slice"; fails=$((fails+1)); }

refuses() {  # refuses <file> <words in the error> <what>
  local err
  if err="$("$TOOL" "$1" /dev/null "$T/out" 2>&1)"; then echo "FAIL: accepted $3"; fails=$((fails+1)); return; fi
  grep -qi "$2" <<<"$err" || { echo "FAIL: $3: unexpected error: $err"; fails=$((fails+1)); }
  [ ! -e "$T/out" ] || { echo "FAIL: $3: output written"; fails=$((fails+1)); rm -f "$T/out"; }
}
refuses "$T/x86" "arm64" "an x86_64-only binary"
head -c 1000 "$T/f/fixture" > "$T/trunc"
refuses "$T/trunc" "malformed\|truncated" "a truncated binary"
# Encrypted: an LC_ENCRYPTION_INFO_64 with cryptid 1 added in the header padding.
python3 -I - "$T/f/fixture" "$T/enc" <<'PY'
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
ncmds, size = struct.unpack_from('<II', d, 16)
struct.pack_into('<IIIIII', d, 32 + size, 0x2C, 24, 0x4000, 0x1000, 1, 0)
struct.pack_into('<II', d, 16, ncmds + 1, size + 24)
open(sys.argv[2], 'wb').write(d)
PY
refuses "$T/enc" "encrypted" "an encrypted binary"
"$TOOL" "$T/f/fixture" "$T/missing.tsv" "$T/out" 2>/dev/null && { echo "FAIL: a missing hooks file accepted"; fails=$((fails+1)); }
# Malformed input is refused without reading or writing past it (convert_tool runs under ASan):
# a dylib load command too short for its name offset, and a hook whose 12-byte patch would end
# past the end of a truncated file.
python3 -I - "$T/f/fixture" "$T/shortdylib" <<'PY'
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
ncmds, size = struct.unpack_from('<II', d, 16)
struct.pack_into('<II', d, 32 + size, 0xC, 8)  # LC_LOAD_DYLIB, cmdsize 8
struct.pack_into('<II', d, 16, ncmds + 1, size + 8)
open(sys.argv[2], 'wb').write(d)
PY
refuses "$T/shortdylib" "malformed" "a dylib command shorter than its fields"
python3 -I - "$T/f/fixture" "$T/f/hooks.tsv" "$T/hookend" "$T/hookend.tsv" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
name, addr = open(sys.argv[2]).readline().split()
addr = int(addr, 16)
ncmds = struct.unpack_from('<I', d, 16)[0]
off = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd == 0x19 and d[off + 8:off + 14] == b'__TEXT':
        vmaddr, vmsize, fileoff = struct.unpack_from('<QQQ', d, off + 24)
    off += size
open(sys.argv[3], 'wb').write(d[:addr - vmaddr + fileoff + 10])
open(sys.argv[4], 'w').write('%s\t%x\n' % (name, addr))
PY
err="$("$TOOL" "$T/hookend" "$T/hookend.tsv" "$T/out" 2>&1)" && { echo "FAIL: a hook past the end accepted"; fails=$((fails+1)); }
grep -q "past the end\|malformed\|truncated" <<<"$err" && ! grep -q "Sanitizer" <<<"$err" \
  || { echo "FAIL: a hook past the end: $err" | head -5; fails=$((fails+1)); }
rm -f "$T/out"

# The real game, when present: same bytes as the Python converter with the launcher's hooks.
GAME_BIN="$ROOT/game-files/ios/Payload/minecraftpe2.app/minecraftpe2"
[ -f "$GAME_BIN" ] || GAME_BIN="$(cd "$ROOT" && git rev-parse --path-format=absolute --git-common-dir 2>/dev/null | sed 's|/.git$||')/game-files/ios/Payload/minecraftpe2.app/minecraftpe2"
if [ -f "$GAME_BIN" ] && [ -x "$ROOT/dist/launcher/mcfm-launch" ]; then
  "$ROOT/dist/launcher/mcfm-launch" --print-hooks > "$T/hooks.tsv"
  bash "$ROOT/tools/launcher/thin_arm64.sh" "$GAME_BIN" "$T/game"
  python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$T/game" "$T/game-py.dylib" --hooks "$T/hooks.tsv"
  "$TOOL" "$GAME_BIN" "$T/hooks.tsv" "$T/game-cc.dylib" && cmp -s "$T/game-py.dylib" "$T/game-cc.dylib" \
    || { echo "FAIL: the game: C++ output differs from mcfm_image.py"; fails=$((fails+1)); }
  echo "convert_test: the game compared"
fi
[ $fails = 0 ] && echo "convert_test: passed" || { echo "$fails failure(s)"; exit 1; }
