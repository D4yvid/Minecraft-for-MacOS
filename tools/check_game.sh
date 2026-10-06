#!/bin/bash
# usage: check_game.sh <Minecraft .app>
# Succeeds only for the build this project supports: Minecraft PE 0.15.10 (iOS), arm64
# slice decrypted. Every platform's packaging script calls this before touching the game.
set -uo pipefail
APP="${1:-}"
GAME_UUID="01DFB489-A881-3BDD-8F98-6F016E409625"  # also in shared/apple/macho_uuid.cpp
die() { echo "check_game: $*" >&2; exit 1; }

[ -d "$APP" ] && [ -f "$APP/Info.plist" ] || die "$APP is not an app bundle"
EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist" 2>/dev/null)"
[ -n "$EXE" ] && [ -f "$APP/$EXE" ] || die "$APP has no executable"
lipo -archs "$APP/$EXE" 2>/dev/null | tr ' ' '\n' | grep -qx arm64 || die "$APP/$EXE has no arm64 slice"
UUID="$(dwarfdump --uuid --arch arm64 "$APP/$EXE" 2>/dev/null | awk '{print $2}')"
[ "$UUID" = "$GAME_UUID" ] || die "not Minecraft PE 0.15.10 (arm64 UUID ${UUID:-none}, expected $GAME_UUID)"
CRYPTID="$(otool -arch arm64 -l "$APP/$EXE" | awk '/LC_ENCRYPTION_INFO/{f=1} f&&/cryptid/{print $2; exit}')"
[ "${CRYPTID:-0}" = "0" ] || die "$APP/$EXE is still encrypted (cryptid $CRYPTID); decrypt it first"
echo "check_game: ok ($APP/$EXE, Minecraft PE 0.15.10, decrypted)"
