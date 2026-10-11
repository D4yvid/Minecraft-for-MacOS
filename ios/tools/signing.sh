# Signing helpers for the iOS builds (sourced by ios/tools/build_app.sh and sign_install.sh):
# the connected device, the provisioning profile Xcode made for a bundle id, the identity.
# MCFM_DECODE_PROFILE (default: security cms -D -i), MCFM_PROFILE_DIRS (colon-separated) and
# MCFM_FIND_IDENTITY (default: security find-identity -v -p codesigning)
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

mcfm_find_identities() {  # the keychain's code signing identities, as security prints them
  if [ -n "${MCFM_FIND_IDENTITY:-}" ]; then $MCFM_FIND_IDENTITY; else security find-identity -v -p codesigning 2>/dev/null; fi
}

# The SHA-1 of the signing identity whose certificate <profile> was made for (one of its
# DeveloperCertificates), or a message saying what to do (exit 1). Another team's or an older
# certificate's identity would sign an app iOS refuses to install.
mcfm_identity() {  # <profile> <bundle id>
  local plist ids rc=0; plist="$(mktemp)"; ids="$(mktemp)"
  mcfm_find_identities > "$ids" || true
  if ! grep -q 'Apple Development' "$ids"; then
    echo "no Apple Development identity in the keychain: sign in to Xcode › Settings › Accounts with your Apple ID" >&2
    rc=1
  elif ! mcfm_decode_profile "$1" > "$plist" || ! python3 -I - "$plist" "$ids" <<'PY'
import hashlib, plistlib, re, sys
try:
    certs = plistlib.load(open(sys.argv[1], 'rb')).get('DeveloperCertificates', [])
except Exception:
    certs = []
wanted = {hashlib.sha1(bytes(c)).hexdigest().upper() for c in certs}
for line in open(sys.argv[2]):
    m = re.match(r'\s*\d+\)\s+([0-9A-Fa-f]{40})\s+"', line)
    if m and m.group(1).upper() in wanted:
        print(m.group(1).upper())
        sys.exit(0)
sys.exit(1)
PY
  then
    echo "the profile for $2 was made for another certificate than the keychain's: open a project with bundle id $2 in Xcode and choose your team again under Signing & Capabilities ($1)" >&2
    rc=1
  fi
  rm -f "$plist" "$ids"
  return $rc
}
