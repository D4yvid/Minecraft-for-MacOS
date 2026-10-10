#!/bin/bash
# setup_game_files.sh builds the local (git-ignored) game-files layout from your own copy.
# usage: setup_game_files_test.sh [decrypted minecraftpe2.app]   (iOS case skipped without it)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
# refuses inputs that are not the supported game, and creates nothing
mkdir -p "$T/fake.app"
if bash "$ROOT/tools/setup_game_files.sh" --out "$T/gf" --ios "$T/fake.app" >/dev/null 2>&1; then fail "accepted a fake app"; fi
[ ! -e "$T/gf/ios/Payload" ] || fail "left files behind after refusing"
if [ -n "${1:-}" ]; then
  bash "$ROOT/tools/setup_game_files.sh" --out "$T/gf" --ios "$1" >/dev/null || fail "setup from the .app failed"
  [ -x "$T/gf/ios/Payload/minecraftpe2.app/minecraftpe2" ] || fail "unzipped app missing"
  [ -f "$T/gf/ios/minecraftpe2.ipa" ] || fail "IPA missing"
  LIST="$(unzip -l "$T/gf/ios/minecraftpe2.ipa")"
  grep -q 'Payload/minecraftpe2.app/minecraftpe2$' <<<"$LIST" || fail "IPA does not contain Payload/minecraftpe2.app"
  [ "$(lipo -archs "$T/gf/ios/minecraftpe2-arm64" 2>/dev/null)" = "arm64" ] || fail "thin arm64 binary missing"
  # and the IPA route gives the same app
  bash "$ROOT/tools/setup_game_files.sh" --out "$T/gf2" --ios "$T/gf/ios/minecraftpe2.ipa" >/dev/null || fail "setup from the .ipa failed"
  cmp -s "$T/gf/ios/Payload/minecraftpe2.app/minecraftpe2" "$T/gf2/ios/Payload/minecraftpe2.app/minecraftpe2" || fail "IPA route gave a different binary"
fi
# the result must stay out of git
git -C "$ROOT" check-ignore -q game-files/ios/minecraftpe2.ipa || fail "game-files/ is not git-ignored"
git -C "$ROOT" check-ignore -q tools/ida/q.py && fail "tools/ida scripts are ignored"
[ $fails = 0 ] && echo "setup_game_files_test: passed" || { echo "$fails failure(s)"; exit 1; }
