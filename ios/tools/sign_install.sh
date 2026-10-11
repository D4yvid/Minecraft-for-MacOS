#!/bin/bash
# usage: sign_install.sh <unsigned.ipa> [<bundle id>]   (make ios-device)
# Signs the IPA from make ios-ipa with your Apple Development identity and installs it on the
# iPhone/iPad connected over USB, then launches it. Needs Xcode signed in to your Apple ID with a
# provisioning profile for <bundle id> (default io.github.d4yvid.mcfm.ios; a free team cannot use
# Mojang's com.mojang.*): created once by choosing your team in any Xcode project with that
# bundle id. Free profiles last 7 days; run it again to renew.
set -euo pipefail
IPA="$1"; BUNDLE_ID="${2:-io.github.d4yvid.mcfm.ios}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
[ -f "$IPA" ] || { echo "sign_install: no IPA at $IPA (make ios-ipa)" >&2; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# The device (the first connected iPhone/iPad).
xcrun devicectl list devices --json-output "$T/devices.json" >/dev/null 2>&1 || true
DEVICE="$(python3 -I -c '
import json, sys
for d in json.load(open(sys.argv[1]))["result"]["devices"]:
    if d.get("connectionProperties", {}).get("pairingState") == "paired" and d.get("hardwareProperties", {}).get("platform") == "iOS":
        print(d["hardwareProperties"]["udid"]); break' "$T/devices.json" 2>/dev/null || true)"
[ -n "$DEVICE" ] || { echo "sign_install: no paired iPhone/iPad connected (xcrun devicectl list devices)" >&2; exit 1; }

# The newest profile for the bundle id that includes the device.
PROFILE=""
for p in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"/*.mobileprovision "$HOME/Library/MobileDevice/Provisioning Profiles"/*.mobileprovision; do
  [ -f "$p" ] || continue
  security cms -D -i "$p" > "$T/p.plist" 2>/dev/null || continue
  if python3 -I - "$T/p.plist" "$BUNDLE_ID" "$DEVICE" <<'PY'
import datetime, plistlib, sys
p = plistlib.load(open(sys.argv[1], 'rb'))
app = p['Entitlements']['application-identifier'].split('.', 1)[1]
ok = app == sys.argv[2] and sys.argv[3] in p.get('ProvisionedDevices', []) and \
     p['ExpirationDate'] > datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
sys.exit(0 if ok else 1)
PY
  then PROFILE="$p"; break; fi
done
[ -n "$PROFILE" ] || { echo "sign_install: no valid provisioning profile for $BUNDLE_ID with this device: choose your team in an Xcode project with bundle id $BUNDLE_ID (Signing & Capabilities)" >&2; exit 1; }
security cms -D -i "$PROFILE" > "$T/profile.plist"
plutil -extract Entitlements xml1 -o "$T/entitlements.plist" "$T/profile.plist"
TEAM="$(plutil -extract TeamIdentifier.0 raw "$T/profile.plist")"
# The identity: an Apple Development certificate of the profile's team.
IDENTITY="$(security find-identity -v -p codesigning | awk -v team="$TEAM" '/Apple Development/ {print $2; exit}')"
[ -n "$IDENTITY" ] || { echo "sign_install: no Apple Development identity (sign in to Xcode › Settings › Accounts)" >&2; exit 1; }

# The app: our bundle id, the profile embedded, the frameworks signed first, then the app.
mkdir "$T/x" && unzip -q "$IPA" -d "$T/x"
APP="$(echo "$T"/x/Payload/*.app)"
plutil -replace CFBundleIdentifier -string "$BUNDLE_ID" "$APP/Info.plist"
plutil -replace CFBundleDisplayName -string "Minecraft PE (mcfm)" "$APP/Info.plist"
rm -rf "$APP/_CodeSignature" "$APP/embedded.mobileprovision"
cp "$PROFILE" "$APP/embedded.mobileprovision"
find "$APP" \( -name '*.dylib' -o -name '*.framework' \) -path '*/Frameworks/*' -maxdepth 3 | while read -r f; do
  codesign -f -s "$IDENTITY" --timestamp=none "$f" >/dev/null
done
codesign -f -s "$IDENTITY" --timestamp=none --entitlements "$T/entitlements.plist" "$APP" >/dev/null
codesign --verify --deep --strict "$APP"
echo "sign_install: signed $(basename "$APP") as $BUNDLE_ID (team $TEAM, profile until $(plutil -extract ExpirationDate raw "$T/profile.plist"))"
xcrun devicectl device install app --device "$DEVICE" "$APP" >/dev/null
echo "sign_install: installed on $DEVICE"
if ! xcrun devicectl device process launch --device "$DEVICE" "$BUNDLE_ID" >/dev/null 2>"$T/launch.err"; then
  echo "sign_install: installed, but iOS did not launch it:"
  sed 's/^/  /' "$T/launch.err" | head -5
  echo "  The first time, trust the developer on the device: Settings › General › VPN & Device Management › your Apple ID › Trust, then open the app."
  exit 0
fi
echo "sign_install: launched"
