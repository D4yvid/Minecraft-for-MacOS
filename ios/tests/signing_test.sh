#!/bin/bash
# ios/tools/signing.sh: the provisioning profile lookup picks the newest unexpired profile for
# the bundle id that includes the device, and finds none otherwise. Fixture profiles are plain
# plists (MCFM_DECODE_PROFILE=cat instead of security cms -D). No device or Xcode needed.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
profile() {  # <file> <app id> <expiry> <device...>
  local f="$1" app="$2" exp="$3"; shift 3
  { echo '<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>'
    echo "<key>ExpirationDate</key><date>$exp</date><key>TeamIdentifier</key><array><string>TEAM123456</string></array>"
    echo "<key>Entitlements</key><dict><key>application-identifier</key><string>TEAM123456.$app</string></dict>"
    echo '<key>ProvisionedDevices</key><array>'; for d in "$@"; do echo "<string>$d</string>"; done; echo '</array>'
    echo '</dict></plist>'; } > "$T/profiles/$f"
}
mkdir -p "$T/profiles"
profile a-expired.mobileprovision io.example.app 2001-01-01T00:00:00Z DEV1
profile b-other-app.mobileprovision io.example.other 2099-01-01T00:00:00Z DEV1
profile c-no-device.mobileprovision io.example.app 2099-01-01T00:00:00Z DEV2
profile d-good.mobileprovision io.example.app 2098-01-01T00:00:00Z DEV2 DEV1
profile e-wildcard-team.mobileprovision '*' 2099-01-01T00:00:00Z DEV1
export MCFM_DECODE_PROFILE=cat MCFM_PROFILE_DIRS="$T/profiles"
# shellcheck source=/dev/null
. "$ROOT/ios/tools/signing.sh"
[ "$(mcfm_find_profile io.example.app DEV1)" = "$T/profiles/d-good.mobileprovision" ] || fail "did not pick the valid profile: '$(mcfm_find_profile io.example.app DEV1)'"
[ -z "$(mcfm_find_profile io.example.app DEV3)" ] || fail "a profile without the device was picked"
[ -z "$(mcfm_find_profile io.example.none DEV1)" ] || fail "a profile for another bundle id was picked"
[ "$(mcfm_profile_team "$T/profiles/d-good.mobileprovision")" = TEAM123456 ] || fail "team not read"
msg="$(mcfm_require_profile io.example.none DEV1 2>&1 >/dev/null)"; rc=$?
[ $rc != 0 ] && grep -q "io.example.none" <<<"$msg" && grep -qi "xcode" <<<"$msg" || fail "no clear message without a profile (rc $rc): $msg"
[ $fails = 0 ] && echo "signing_test: passed" || { echo "$fails failure(s)"; exit 1; }
