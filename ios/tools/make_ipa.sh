#!/bin/bash
# usage: make_ipa.sh <decrypted Minecraft .app or .ipa> <out.ipa> <libmcfm.dylib>
# Produces an unsigned IPA with the mod injected. Sign / sideload it with your own tools
# (they re-sign the app and its Frameworks).
set -euo pipefail
GAME="$1"; OUT="$2"; DYLIB="$3"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"  # repo root
die() { echo "make_ipa: $*" >&2; exit 1; }

[ -f "$DYLIB" ] || die "dylib $DYLIB not found (build it with make ios)"
[ -e "$GAME" ] || die "game $GAME not found"
GAME="${GAME%/}"; GAME="${GAME%/}"
GAME="$(cd "$(dirname "$GAME")" && pwd)/$(basename "$GAME")"  # absolute: we cd later
case "$OUT" in *.ipa) ;; *) die "output must end in .ipa: $OUT" ;; esac
DYLIB="$(cd "$(dirname "$DYLIB")" && pwd)/$(basename "$DYLIB")"
OUT_DIR="$(mkdir -p "$(dirname "$OUT")" && cd "$(dirname "$OUT")" && pwd)"
OUT="$OUT_DIR/$(basename "$OUT")"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
case "$GAME" in
  *.ipa) (cd "$WORK" && unzip -q "$GAME") || die "cannot unzip $GAME" ;;
  *.app) mkdir "$WORK/Payload" && cp -R "$GAME" "$WORK/Payload/" ;;
  *) die "game must be a .app or .ipa: $GAME" ;;
esac
APPS=("$WORK"/Payload/*.app)
[ ${#APPS[@]} = 1 ] && [ -d "${APPS[0]}" ] || die "expected exactly one Payload/*.app in $GAME"
APP="${APPS[0]}"
bash "$ROOT/tools/check_game.sh" "$APP" >/dev/null || die "unsupported game (see message above)"
EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist")"

# Every iOS device that runs current sideloading tools is arm64; drop the armv7 slice so
# the load command only has to go into one slice.
if [ "$(lipo -archs "$APP/$EXE")" != "arm64" ]; then
  lipo "$APP/$EXE" -thin arm64 -output "$APP/$EXE.thin" && mv "$APP/$EXE.thin" "$APP/$EXE"
fi
# Full screen: iOS letterboxes apps linked against an SDK older than iOS 11 on current iPhones
# (the game was built with 9.3). Only the SDK field changes (to 11.0, the fewest "linked on or
# after" behaviour changes); the deployment target stays.
python3 -I - "$APP/$EXE" <<'PY' || die "cannot set the binary's SDK version"
import struct, sys
path = sys.argv[1]
d = bytearray(open(path, 'rb').read())
magic, _, _, _, ncmds = struct.unpack_from('<IiiII', d, 0)
assert magic == 0xFEEDFACF, 'not a thin 64-bit Mach-O'
off, sdk11, done = 32, 11 << 16, 0
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    field = {0x25: off + 12, 0x32: off + 16}.get(cmd)  # LC_VERSION_MIN_IPHONEOS sdk, LC_BUILD_VERSION sdk
    if field:
        if struct.unpack_from('<I', d, field)[0] < sdk11:
            struct.pack_into('<I', d, field, sdk11)
        done += 1
    off += size
assert done, 'no version load command'
open(path, 'wb').write(d)
PY
# Declared a game (Game Mode, the App Library's Games category); full screen, no status bar.
for kv in "LSApplicationCategoryType string public.app-category.games" "GCSupportsGameMode bool true" \
          "UIRequiresFullScreen bool true" "UIStatusBarHidden bool true" "UIViewControllerBasedStatusBarAppearance bool false"; do
  set -- $kv
  /usr/libexec/PlistBuddy -c "Delete :$1" "$APP/Info.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$1 $2 $3" "$APP/Info.plist"
done
mkdir -p "$APP/Frameworks"
cp "$DYLIB" "$APP/Frameworks/libmcfm.dylib"
python3 -I "$ROOT/tools/inject.py" "$APP/$EXE" @executable_path/Frameworks/libmcfm.dylib >/dev/null
rm -rf "$APP/_CodeSignature"
xattr -cr "$WORK/Payload"
rm -f "$OUT"
(cd "$WORK" && zip -qry "$OUT" Payload)
echo "make_ipa: wrote $OUT (unsigned; sign it before installing)"
