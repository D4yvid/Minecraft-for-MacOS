#!/bin/bash
# usage: ipa_test.sh <decrypted Minecraft .app or .ipa>
# Builds an IPA with a stand-in iOS dylib and checks its structure.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME="${1:?usage: ipa_test.sh <game .app or .ipa>}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
# Stand-in iOS dylib. Without Xcode there is no iOS SDK, so link against a tiny libSystem stub.
printf 'int mcfm_stub;\n' > "$T/stub.c"
cat > "$T/libSystem.tbd" <<'TBD'
--- !tapi-tbd
tbd-version: 4
targets: [ arm64-ios ]
install-name: '/usr/lib/libSystem.B.dylib'
...
TBD
clang -target arm64-apple-ios12.0 -Wno-incompatible-sysroot -dynamiclib -nostdlib -L"$T" -lSystem \
  -install_name @executable_path/Frameworks/libmcfm.dylib "$T/stub.c" -o "$T/libmcfm.dylib" || { echo "cannot build stub dylib"; exit 1; }
bash "$ROOT/ios/tools/make_ipa.sh" "$GAME" "$T/out.ipa" "$T/libmcfm.dylib" >/dev/null || fail "make_ipa.sh failed"
[ -f "$T/out.ipa" ] || { echo "no IPA produced"; exit 1; }
mkdir "$T/x" && (cd "$T/x" && unzip -q "$T/out.ipa")
APPS=("$T"/x/Payload/*.app)
[ ${#APPS[@]} = 1 ] && [ -d "${APPS[0]}" ] || fail "expected exactly one Payload/*.app"
APP="${APPS[0]}"
EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist")"
[ "$(lipo -archs "$APP/$EXE")" = "arm64" ] || fail "main binary is not arm64-only: $(lipo -archs "$APP/$EXE")"
otool -L "$APP/$EXE" | grep -q '@executable_path/Frameworks/libmcfm.dylib' || fail "LC_LOAD_DYLIB missing"
[ -f "$APP/Frameworks/libmcfm.dylib" ] || fail "dylib not in Frameworks/"
vtool -show-build "$APP/Frameworks/libmcfm.dylib" 2>/dev/null | grep -q 'platform IOS$' || fail "dylib is not an iOS build"
vtool -show-build "$APP/$EXE" 2>/dev/null | grep -q 'MACCATALYST' && fail "main binary was retagged for Mac"
[ ! -e "$APP/_CodeSignature" ] || fail "stale _CodeSignature left in the app"
# Full screen on current iPhones: iOS letterboxes apps linked against an SDK older than 11 (the
# game says 9.3); the deployment target stays as it was.
SDK_V="$(otool -l "$APP/$EXE" | awk '/LC_VERSION_MIN_IPHONEOS|LC_BUILD_VERSION/ {f=1} f && $1=="sdk" {print $2; exit}')"
[ "${SDK_V%%.*}" -ge 11 ] 2>/dev/null || fail "main binary still linked against SDK $SDK_V (letterboxed on current iPhones)"
otool -l "$APP/$EXE" | awk '/LC_VERSION_MIN_IPHONEOS/ {f=1} f && $1=="version" {print $2; exit}' | grep -qx '8.0' \
  || fail "the deployment target changed"
# A game, full screen, without the status bar.
plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$APP/Info.plist" 2>/dev/null; }
[ "$(plist LSApplicationCategoryType)" = public.app-category.games ] || fail "not declared a game (LSApplicationCategoryType)"
[ "$(plist GCSupportsGameMode)" = true ] || fail "GCSupportsGameMode missing"
[ "$(plist UIStatusBarHidden)" = true ] && [ "$(plist UIViewControllerBasedStatusBarAppearance)" = false ] || fail "the status bar is not hidden"
[ "$(plist UIRequiresFullScreen)" = true ] || fail "UIRequiresFullScreen missing"
# Relative paths and a trailing slash on the .app must work too.
APP_ABS="$(cd "$GAME" && pwd)"
(cd "$(dirname "$APP_ABS")" && bash "$ROOT/ios/tools/make_ipa.sh" "$(basename "$APP_ABS")/" "$T/slash.ipa" "$T/libmcfm.dylib" >/dev/null) \
  || fail "relative .app path with a trailing slash rejected"
(cd "$T" && bash "$ROOT/ios/tools/make_ipa.sh" out.ipa again.ipa libmcfm.dylib >/dev/null) || fail "relative .ipa input rejected"
[ -f "$T/again.ipa" ] || fail "no IPA written for relative paths"
[ $fails = 0 ] && echo "ipa_test: passed" || { echo "$fails failure(s)"; exit 1; }
