#!/bin/bash
# check_game.sh must accept only the decrypted 0.15.10 build.
# usage: check_game_test.sh [real minecraftpe2.app]   (real-app case skipped without it)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
mkdir -p "$T/fake.app"
printf 'int main(void){return 0;}\n' > "$T/m.c"; clang -arch arm64 "$T/m.c" -o "$T/fake.app/fake"
/usr/libexec/PlistBuddy -c 'Add :CFBundleExecutable string fake' "$T/fake.app/Info.plist" >/dev/null
if bash "$ROOT/tools/check_game.sh" "$T/fake.app" >/dev/null 2>&1; then echo "FAIL: accepted a non-Minecraft app"; fails=$((fails+1)); fi
if bash "$ROOT/tools/check_game.sh" "$T/missing.app" >/dev/null 2>&1; then echo "FAIL: accepted a missing app"; fails=$((fails+1)); fi
msg="$(bash "$ROOT/tools/check_game.sh" "$T/fake.app" 2>&1)"
grep -q "0.15.10" <<<"$msg" || { echo "FAIL: rejection message does not name the supported build: $msg"; fails=$((fails+1)); }
if [ -n "${1:-}" ]; then
  bash "$ROOT/tools/check_game.sh" "$1" >/dev/null || { echo "FAIL: rejected the real game $1"; fails=$((fails+1)); }
fi
[ $fails = 0 ] && echo "check_game_test: passed" || { echo "$fails failure(s)"; exit 1; }
