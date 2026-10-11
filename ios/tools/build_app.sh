#!/bin/bash
# usage: build_app.sh <minecraftpe.ipa> <out dir> [<bundle id>]   (make ios-app)
# ANGLE: $ANGLE_IOS (default build/angle-ios/libGLESv2.dylib, from make angle-ios).
# Our iOS app (docs/LAUNCHER.md, Stage 4), built from the command line: the game binary from the
# IPA converted into a dylib for iOS (dyld loads it; iOS runs no code that was not signed when
# the app was built) with the launcher's hooks, stubs for the frameworks the game's iOS glue
# imports (OpenGLES re-exports ANGLE, OpenGL ES 3 on Metal; AudioToolbox stays the system's),
# the launcher (ios/launcher, shared/)
# and the Swift app (ios/app) linked into one executable, the game's data/ bundled, all signed
# with your Apple Development identity and the profile Xcode made for <bundle id>.
# Writes <out>/mcfm.app and <out>/mcfm.ipa. The app holds Mojang's files: a local build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IPA="$1"; OUT="$2"; BUNDLE_ID="${3:-io.github.d4yvid.mcfm.ios}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
[ -f "$IPA" ] || { echo "build_app: no IPA at $IPA" >&2; exit 2; }
ANGLE_IOS="${ANGLE_IOS:-$ROOT/build/angle-ios/libGLESv2.dylib}"
[ -f "$ANGLE_IOS" ] || { echo "build_app: no ANGLE for iOS at $ANGLE_IOS (make angle-ios)" >&2; exit 2; }
SDK="$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)" || { echo "build_app: no iPhoneOS SDK (install Xcode)" >&2; exit 2; }
TARGET=arm64-apple-ios15.0
W="$ROOT/build/ios-app"; APP="$OUT/mcfm.app"
rm -rf "$W" "$APP" && mkdir -p "$W/obj" "$W/stubs" "$APP/Frameworks" "$APP/game"
run() { local out; out="$("$@" 2>&1)" || { echo "build_app: $(basename "$1") failed:" >&2; echo "$out" >&2; exit 1; }; }

# The game: binary, data/, icons from the IPA.
python3 -I - "$IPA" "$W/ipa" <<'PY'
import sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(src) as z:
    names = [n for n in z.namelist() if n.startswith('Payload/') and n.count('/') >= 2]
    app = names[0].split('/')[1] if names else ''
    if not app.endswith('.app'):
        sys.exit('build_app: no Payload/*.app in ' + src)
    for n in names:
        rest = n.split('/', 2)[2]
        if '..' in rest.split('/') or n.endswith('/'):
            continue
        if rest == 'minecraftpe2' or rest.startswith('data/') or (rest.startswith('AppIcon') and rest.endswith('.png')):
            z.extract(n, out)
PY
GAME_APP="$(echo "$W"/ipa/Payload/*.app)"
[ -f "$GAME_APP/minecraftpe2" ] || { echo "build_app: no minecraftpe2 in $IPA" >&2; exit 2; }
run bash "$ROOT/tools/launcher/thin_arm64.sh" "$GAME_APP/minecraftpe2" "$W/game"
run clang++ -std=c++11 -O1 -I"$ROOT/shared/launcher" -I"$ROOT/shared/apple" -I"$ROOT/shared/include" \
  "$ROOT/ios/tools/print_hooks.cpp" "$ROOT/shared/launcher/seams.cpp" -o "$W/print_hooks"
"$W/print_hooks" > "$W/hooks.tsv"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$W/game" > "$W/imports.tsv"
run python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$W/game" "$APP/Frameworks/libminecraftpe.dylib" \
  --hooks "$W/hooks.tsv" --platform ios --host AudioToolbox
cp "$ANGLE_IOS" "$APP/Frameworks/libGLESv2.dylib"
run bash "$ROOT/tools/launcher/build_stubs.sh" --target ios "$W/imports.tsv" "$W/stubs" --skip AudioToolbox \
  --provider OpenGLES="$APP/Frameworks/libGLESv2.dylib"
cp "$W"/stubs/*.dylib "$APP/Frameworks/"
cp -R "$GAME_APP/data" "$APP/game/data"

# The executable: the launcher (C++/Objective-C++) and the Swift app.
SRCS=(shared/apple/macho_uuid.cpp shared/apple/hook_table.cpp shared/launcher/app_platform.cpp shared/launcher/engine.cpp
      shared/launcher/seams.cpp shared/launcher/text_input.cpp shared/launcher/launcher_platform.cpp shared/launcher/touch_input.cpp
      shared/src/keyboard_mouse.cpp shared/src/platform.cpp shared/src/keymap.cpp ios/launcher/ios_keymap.cpp ios/launcher/launcher.mm
      ios/launcher/egl_view.mm)
OBJS=()
for s in "${SRCS[@]}"; do
  o="$W/obj/$(echo "$s" | tr / _).o"
  run xcrun --sdk iphoneos clang++ -target "$TARGET" -isysroot "$SDK" -std=c++17 -fobjc-arc -O2 -Wall -Wextra \
    -Wno-unused-parameter -I"$ROOT/shared/include" -I"$ROOT/shared/apple" -I"$ROOT/shared/launcher" -I"$ROOT/ios/launcher" -I"$ROOT/macos/launcher" \
    -c "$ROOT/$s" -o "$o"
  OBJS+=("$o")
done
run xcrun --sdk iphoneos swiftc -swift-version 5 -O -target "$TARGET" -sdk "$SDK" \
  -import-objc-header "$ROOT/ios/app/Bridging.h" -I"$ROOT/ios/launcher" "$ROOT"/ios/app/*.swift "${OBJS[@]}" \
  -lc++ "$APP/Frameworks/libGLESv2.dylib" -framework QuartzCore -framework UIKit -framework AVFoundation -framework PhotosUI \
  -framework UniformTypeIdentifiers -framework ImageIO -Xlinker -rpath -Xlinker @executable_path/Frameworks \
  -o "$APP/mcfm"

# The bundle: Info.plist, launch screen, icons.
VERSION="0.1"
sed -e "s/@BUNDLE_ID@/$BUNDLE_ID/" -e "s/@VERSION@/$VERSION/" "$ROOT/ios/app/Info.plist" > "$APP/Info.plist"
run xcrun ibtool --compile "$APP/LaunchScreen.storyboardc" "$ROOT/ios/app/LaunchScreen.storyboard" \
  --target-device iphone --target-device ipad --minimum-deployment-target 15.0
ICONS=()
for f in "$GAME_APP"/AppIcon*.png; do [ -f "$f" ] && cp "$f" "$APP/" && ICONS+=("$(basename "$f" .png)"); done
if [ ${#ICONS[@]} -gt 0 ]; then
  /usr/libexec/PlistBuddy -c "Add :CFBundleIcons dict" -c "Add :CFBundleIcons:CFBundlePrimaryIcon dict" \
    -c "Add :CFBundleIcons:CFBundlePrimaryIcon:CFBundleIconFiles array" "$APP/Info.plist"
  for i in "${!ICONS[@]}"; do
    /usr/libexec/PlistBuddy -c "Add :CFBundleIcons:CFBundlePrimaryIcon:CFBundleIconFiles:$i string ${ICONS[$i]%%@*}" "$APP/Info.plist"
  done
fi
printf 'APPL????' > "$APP/PkgInfo"

# Signing: the frameworks, then the app with the profile's entitlements.
# shellcheck source=signing.sh
. "$ROOT/ios/tools/signing.sh"
DEVICE="${MCFM_DEVICE:-$(mcfm_find_device)}"
[ -n "$DEVICE" ] || { echo "build_app: no paired iPhone/iPad connected: the profile is chosen for it" >&2; exit 1; }
PROFILE="$(mcfm_require_profile "$BUNDLE_ID" "$DEVICE")" || exit 1
IDENTITY="$(mcfm_identity)"
[ -n "$IDENTITY" ] || { echo "build_app: no Apple Development identity (Xcode › Settings › Accounts)" >&2; exit 1; }
cp "$PROFILE" "$APP/embedded.mobileprovision"
mcfm_profile_entitlements "$PROFILE" "$W/entitlements.plist"
for f in "$APP"/Frameworks/*.dylib; do run codesign -f -s "$IDENTITY" --timestamp=none "$f"; done
run codesign -f -s "$IDENTITY" --timestamp=none --entitlements "$W/entitlements.plist" "$APP"
run codesign --verify --deep --strict "$APP"
rm -rf "$W/Payload" && mkdir "$W/Payload" && cp -R "$APP" "$W/Payload/"
rm -f "$OUT/mcfm.ipa" && (cd "$W" && zip -qry "$OUT/mcfm.ipa" Payload)
echo "build_app: $APP ($BUNDLE_ID, team $(mcfm_profile_team "$PROFILE"), $(du -sh "$APP" | awk '{print $1}'))"
