#!/bin/bash
# mcfm_image.py imports: lists every import with its library and function/data kind.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" || { echo "FAIL: fixture build"; exit 1; }
OUT="$(python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$T/fixture")" || { echo "FAIL: imports exited $?"; exit 1; }
fails=0
expect() { grep -qxF "$1" <<<"$OUT" || { echo "FAIL: missing line: $1"; fails=$((fails+1)); }; }
expect $'FakeKit\t_fakekit_hello\tfn'
expect $'FakeKit\t_kFakeKitValue\tdata'
expect $'libobjc\t_objc_msgSend\tfn'
expect $'libobjc\t_OBJC_CLASS_$_NSObject\tdata'
expect $'libobjc\t_OBJC_METACLASS_$_NSObject\tdata'
expect $'FakeKit\t-\tlib'
expect $'libobjc\t-\tlib'
expect $'libSystem\t-\tlib'
[ "$(sort -u <<<"$OUT")" = "$OUT" ] || { echo "FAIL: output not sorted/unique"; fails=$((fails+1)); }
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$T/nope" 2>/dev/null && { echo "FAIL: missing file accepted"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_imports_test: passed" || { echo "$fails failure(s)"; exit 1; }
