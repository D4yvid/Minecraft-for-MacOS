#!/bin/bash
# usage: app_check.sh <mcfm.app> <out dir>   (make ios-app-check)
# Stage 4 acceptance on the connected iPhone/iPad (docs/LAUNCHER.md): installs the app, launches
# it with --frames 300 --screenshot shot.ppm (the phone must be unlocked), waits for the engine
# and the frames, copies the last frame out of the app's container and checks it: the title
# screen (tools/android/check_screenshot.py) at the screen's full native size.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$1"; OUT="$2"; BUNDLE_ID="${3:-io.github.d4yvid.mcfm.ios}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
[ -d "$APP" ] || { echo "app_check: no app at $APP (make ios-app)" >&2; exit 2; }
# shellcheck source=signing.sh
. "$ROOT/ios/tools/signing.sh"
DEVICE="$(mcfm_find_device)"
[ -n "$DEVICE" ] || { echo "app_check: no paired iPhone/iPad connected" >&2; exit 1; }
mkdir -p "$OUT"
fail() {  # the console's last lines, if the app ran at all
  echo "app_check: FAIL ($1)"
  [ -f "$OUT/ios-console.log" ] && { grep 'mcfm' "$OUT/ios-console.log" | grep -v 'mcfm: stub ' | tail -20 || true; }
  exit 1
}
# Never a stale console or screenshot: the app also deletes Documents/<screenshot> when it starts.
rm -f "$OUT/ios-console.log" "$OUT/ios-shot.ppm" "$OUT/ios-shot.png"
xcrun devicectl device install app --device "$DEVICE" "$APP" > "$OUT/ios-install.log" 2>&1 || { cat "$OUT/ios-install.log"; fail "install"; }
# The console runs until the app exits (it does after the frames); 120 s at most.
( xcrun devicectl device process launch --console --terminate-existing --device "$DEVICE" "$BUNDLE_ID" \
    --frames 300 --screenshot shot.ppm > "$OUT/ios-console.log" 2>&1 ) &
pid=$!
for _ in $(seq 1 120); do kill -0 $pid 2>/dev/null || break; sleep 1; done
kill $pid 2>/dev/null || true
grep -q 'Unable to launch' "$OUT/ios-console.log" && fail "iOS refused to launch it (locked phone, or the certificate not trusted)"
grep -q 'mcfm: engine started' "$OUT/ios-console.log" || fail "the engine did not start"
grep -q 'mcfm: 300 frames rendered' "$OUT/ios-console.log" || fail "300 frames were not rendered (is the phone unlocked?)"
xcrun devicectl device copy from --device "$DEVICE" --domain-type appDataContainer --domain-identifier "$BUNDLE_ID" \
  --source Documents/shot.ppm --destination "$OUT/ios-shot.ppm" >/dev/null 2>&1 || fail "no screenshot in the app's Documents"
SIZE="$(head -c 32 "$OUT/ios-shot.ppm" | sed -n 2p)"
STARTED="$(grep -o 'engine started ([0-9]*x[0-9]*)' "$OUT/ios-console.log" | head -1 | grep -o '[0-9]*x[0-9]*')"
[ "$SIZE" = "${STARTED/x/ }" ] || fail "the frame is $SIZE, the engine started at $STARTED"
python3 -I "$ROOT/tools/android/check_screenshot.py" "$OUT/ios-shot.ppm" || fail "the title screen did not show"
sips -s format png "$OUT/ios-shot.ppm" --out "$OUT/ios-shot.png" >/dev/null 2>&1 || true
echo "app_check: passed ($STARTED, $OUT/ios-shot.png)"
