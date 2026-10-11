# Signing helpers for the iOS builds (sourced by ios/tools/build_app.sh and sign_install.sh):
# the connected device, the provisioning profile Xcode made for a bundle id, the identity.
# MCFM_DECODE_PROFILE (default: security cms -D -i) and MCFM_PROFILE_DIRS (colon-separated)
# are for the tests.
MCFM_PROFILE_DIRS="${MCFM_PROFILE_DIRS:-$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles:$HOME/Library/MobileDevice/Provisioning Profiles}"

mcfm_decode_profile() {  # <profile> -> its plist on stdout
  if [ -n "${MCFM_DECODE_PROFILE:-}" ]; then $MCFM_DECODE_PROFILE "$1"; else security cms -D -i "$1" 2>/dev/null; fi
}

# The UDID of the first paired iPhone/iPad (empty if none).
mcfm_find_device() {
  local json; json="$(mktemp)"
  xcrun devicectl list devices --json-output "$json" >/dev/null 2>&1 || true
  python3 -I -c '
import json, sys
try:
    devices = json.load(open(sys.argv[1]))["result"]["devices"]
except Exception:
    devices = []
for d in devices:
    if d.get("connectionProperties", {}).get("pairingState") == "paired" and d.get("hardwareProperties", {}).get("platform") == "iOS":
        print(d["hardwareProperties"]["udid"]); break' "$json"
  rm -f "$json"
}

# The unexpired profile for exactly <bundle id> that includes <udid>, the one expiring last.
mcfm_find_profile() {  # <bundle id> <udid>
  local id="$1" udid="$2" best="" best_exp="" dir p exp plist
  plist="$(mktemp)"
  local IFS=:
  for dir in $MCFM_PROFILE_DIRS; do
    for p in "$dir"/*.mobileprovision; do
      [ -f "$p" ] || continue
      mcfm_decode_profile "$p" > "$plist" || continue
      exp="$(python3 -I - "$plist" "$id" "$udid" <<'PY'
import datetime, plistlib, sys
try:
    p = plistlib.load(open(sys.argv[1], 'rb'))
    app = p['Entitlements']['application-identifier'].split('.', 1)[1]
    exp = p['ExpirationDate']
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    if app == sys.argv[2] and sys.argv[3] in p.get('ProvisionedDevices', []) and exp > now:
        print(exp.strftime('%Y%m%d%H%M%S'))
except Exception:
    pass
PY
)"
      if [ -n "$exp" ] && { [ -z "$best_exp" ] || [ "$exp" \> "$best_exp" ]; }; then best="$p"; best_exp="$exp"; fi
    done
  done
  rm -f "$plist"
  echo "$best"
}

# As mcfm_find_profile, or a message saying how to get one (exit 1).
mcfm_require_profile() {  # <bundle id> <udid>
  local p; p="$(mcfm_find_profile "$1" "$2")"
  if [ -z "$p" ]; then
    echo "no valid provisioning profile for $1 with device $2: in Xcode, open a project with bundle id $1, choose your team under Signing & Capabilities (free profiles last 7 days)" >&2
    return 1
  fi
  echo "$p"
}

mcfm_profile_team() {  # <profile>
  local plist; plist="$(mktemp)"
  mcfm_decode_profile "$1" > "$plist" && plutil -extract TeamIdentifier.0 raw "$plist"
  rm -f "$plist"
}

mcfm_profile_entitlements() {  # <profile> <out.plist>
  local plist; plist="$(mktemp)"
  mcfm_decode_profile "$1" > "$plist" && plutil -extract Entitlements xml1 -o "$2" "$plist"
  rm -f "$plist"
}

# The SHA-1 of an Apple Development identity (empty if none).
mcfm_identity() {
  security find-identity -v -p codesigning | awk '/Apple Development/ {print $2; exit}'
}
