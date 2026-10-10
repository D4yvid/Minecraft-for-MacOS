#!/bin/bash
# mcfm_image.py dylib on unusual input: malformed load commands, other dylib load kinds, two
# platform tags, encryption, and hook addresses that cannot carry a 12-byte patch.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL=(python3 -I "$ROOT/tools/launcher/mcfm_image.py")
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
fails=0
# mutate <in> <out> <python using `cmds` = [(offset, cmd, size)] and bytearray `b`>
mutate() {
  python3 - "$1" "$2" "$3" <<'PY'
import struct, sys
b = bytearray(open(sys.argv[1], "rb").read())
ncmds = struct.unpack_from("<I", b, 16)[0]
cmds, off = [], 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", b, off)
    cmds.append((off, cmd, size)); off += size
def find(cmd, nth=0):
    return [c for c in cmds if c[1] == cmd][nth]
exec(sys.argv[3])
open(sys.argv[2], "wb").write(b)
PY
}
refused() {  # <name> <input> <message>
  "${TOOL[@]}" dylib "$2" "$T/out-$1.dylib" 2>"$T/err-$1"; local rc=$?
  { [ $rc = 2 ] && grep -q "$3" "$T/err-$1" && ! grep -q Traceback "$T/err-$1" && [ ! -e "$T/out-$1.dylib" ]; } \
    || { echo "FAIL: $1: want exit 2 with '$3' (rc $rc): $(tail -1 "$T/err-$1")"; fails=$((fails+1)); }
}
mutate "$T/fixture" "$T/zero" 'struct.pack_into("<I", b, cmds[2][0] + 4, 0)'
refused zero-cmdsize "$T/zero" "malformed"
mutate "$T/fixture" "$T/many" 'struct.pack_into("<I", b, 16, 100000)'
refused too-many-cmds "$T/many" "malformed"
mutate "$T/fixture" "$T/notext" 'o = [c for c in cmds if c[1] == 0x19 and b[c[0]+8:c[0]+14] == b"__TEXT"][0][0]; b[o+8:o+14] = b"__TEXX"'
refused no-text "$T/notext" "no __TEXT"
mutate "$T/fixture" "$T/crypt" 'o = find(0x1B)[0]; struct.pack_into("<IIIIII", b, o, 0x2C, 24, 0x4000, 0x4000, 1, 0)'
refused encrypted "$T/crypt" "encrypted"
# Other dylib load commands are redirected like LC_LOAD_DYLIB.
for kind in 0x8000001F 0x20 0x80000023; do
  mutate "$T/fixture" "$T/k$kind" "
for o, cmd, size in cmds:
    if cmd == 0xC and b'FakeKit' in b[o:o+size]: struct.pack_into('<I', b, o, $kind)"
  "${TOOL[@]}" dylib "$T/k$kind" "$T/k$kind.dylib" 2>/dev/null || { echo "FAIL: load kind $kind refused"; fails=$((fails+1)); continue; }
  otool -l "$T/k$kind.dylib" | grep -q "@rpath/mcfm_stub_FakeKit.dylib" || { echo "FAIL: load kind $kind not redirected"; fails=$((fails+1)); }
done
# Two platform tags (LC_VERSION_MIN_IPHONEOS + LC_BUILD_VERSION) -> exactly one LC_BUILD_VERSION.
mutate "$T/fixture" "$T/twotags" 'o = find(0x2A)[0]; struct.pack_into("<IIII", b, o, 0x25, 16, 0x000F0000, 0x000F0000)'
"${TOOL[@]}" dylib "$T/twotags" "$T/twotags.dylib" || { echo "FAIL: two tags refused"; fails=$((fails+1)); }
n="$(otool -l "$T/twotags.dylib" 2>/dev/null | grep -c "cmd LC_BUILD_VERSION")"
[ "$n" = 1 ] || { echo "FAIL: $n LC_BUILD_VERSION commands, want 1"; fails=$((fails+1)); }
# Hooks that cannot carry a 12-byte patch are refused, nothing written.
ANSWER="$(nm "$T/fixture" | awk '$3=="_fixture_answer"{print $1}')"
TINY="$(nm "$T/fixture" | awk '$3=="_fixture_tiny"{print $1}')"
hooks() { printf '%s\n' "$@" > "$T/h.tsv"; "${TOOL[@]}" dylib "$T/fixture" "$T/out-h.dylib" --hooks "$T/h.tsv" 2>"$T/err-h"; }
check_hook() {  # <name> <message> <tsv lines...>
  local name="$1" msg="$2"; shift 2
  rm -f "$T/out-h.dylib"; hooks "$@"; local rc=$?
  { [ $rc = 2 ] && grep -q "$msg" "$T/err-h" && [ ! -e "$T/out-h.dylib" ]; } || { echo "FAIL: hook $name: want '$msg' (rc $rc): $(tail -1 "$T/err-h")"; fails=$((fails+1)); }
}
check_hook unaligned "aligned" "$(printf 'a\t0x%x' $((16#$ANSWER + 2)))"
check_hook duplicate "overlap" "$(printf 'a\t0x%s' "$ANSWER")" "$(printf 'b\t0x%s' "$ANSWER")"
check_hook overlapping "overlap" "$(printf 'a\t0x%s' "$ANSWER")" "$(printf 'b\t0x%x' $((16#$ANSWER + 4)))"
check_hook too-short "shorter than 12 bytes" "$(printf 'tiny\t0x%s' "$TINY")"
[ $fails = 0 ] && echo "launcher_converter_edges_test: passed" || { echo "$fails failure(s)"; exit 1; }
