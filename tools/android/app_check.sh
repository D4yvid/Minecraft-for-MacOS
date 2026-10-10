#!/bin/bash
# usage: app_check.sh <mcfm.apk> <minecraftpe2.ipa> <out dir>   (make android-app-check)
# Stage 3c acceptance (docs/LAUNCHER.md) on the running emulator/device, from a clean install:
# the app imports the IPA (debug-only `path` extra, no picker), shows the title screen, Play ->
# Create New World -> Create World! reaches a world (touch input), the game saves when the app
# goes to the background and draws again when it comes back. Screenshots: <out>/app-*.raw.
# The taps are fractions of the screen, measured on the game's GUI at 1280x720 (the emulators).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APK="$1"; IPA="$2"; OUT="$3"
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="$SDK/platform-tools/adb"
PKG=io.github.d4yvid.mcfm
[ -f "$APK" ] || { echo "app_check: no APK at $APK (make android-app)" >&2; exit 2; }
[ -f "$IPA" ] || { echo "app_check: no IPA at $IPA (make android-app-check IPA=<decrypted minecraftpe .ipa>)" >&2; exit 2; }
mkdir -p "$OUT"
fail() { echo "app_check: FAIL ($1)"; "$ADB" logcat -d -s mcfm:I AndroidRuntime:E DEBUG:F libc:F | grep -v 'mcfm: stub ' | tail -30; exit 1; }
log() { "$ADB" logcat -d -s mcfm:I AndroidRuntime:E DEBUG:F libc:F 2>/dev/null; }
crashed() { log | grep -q -e 'FATAL EXCEPTION' -e 'Fatal signal' -e '>>> io.github.d4yvid.mcfm <<<'; }
# wait_log <pattern> <seconds>: until the app logs the pattern; fails on a crash or the timeout.
wait_log() {
  for _ in $(seq 1 "$2"); do
    log | grep -q -e "$1" && return 0
    crashed && fail "the app crashed waiting for '$1'"
    sleep 1
  done
  fail "no '$1' after $2 s"
}
# shot <name> [--world]: a screenshot that passes check_screenshot.py, within 30 s.
shot() {
  for _ in $(seq 1 15); do
    "$ADB" exec-out screencap > "$OUT/app-$1.raw"
    python3 -I "$ROOT/tools/android/check_screenshot.py" ${2:-} "$OUT/app-$1.raw" > "$OUT/app-$1.txt" && { cat "$OUT/app-$1.txt"; return 0; }
    crashed && fail "the app crashed before the $1 screen"
    sleep 2
  done
  cat "$OUT/app-$1.txt"; fail "the $1 screen did not show"
}
# wait_focus: until the game's activity has the window focus (a screenshot before then could be
# the launcher).
wait_focus() {
  for _ in $(seq 1 30); do
    "$ADB" shell dumpsys window | grep -q "mCurrentFocus=.*$PKG/$PKG.GameActivity" && return 0
    crashed && fail "the app crashed before the game came to the front"
    sleep 1
  done
  fail "the game did not come to the front"
}
read -r W H < <("$ADB" shell wm size | awk -F'[ x]' '/size/ {w=$(NF-1); h=$NF} END {print w, h}')
[ "$W" -lt "$H" ] && { t=$W; W=$H; H=$t; }  # the game runs in landscape
tap() { "$ADB" shell input tap "$(awk "BEGIN {print int($W * $1)}")" "$(awk "BEGIN {print int($H * $2)}")"; }

echo "app_check: installing $(basename "$APK") on $("$ADB" shell getprop ro.build.version.release | tr -d '\r') (${W}x${H})"
"$ADB" install -r "$APK" >/dev/null
"$ADB" shell pm clear "$PKG" >/dev/null  # a fresh install: no game, no worlds
# The IPA goes through /data/local/tmp (exec-in into run-as drops bytes); the app reads it there.
"$ADB" push "$IPA" /data/local/tmp/mcfm-import.ipa >/dev/null 2>&1
"$ADB" shell "run-as $PKG sh -c 'mkdir -p cache && cp /data/local/tmp/mcfm-import.ipa cache/import.ipa'" \
  || fail "cannot copy the IPA into the app"
"$ADB" shell rm /data/local/tmp/mcfm-import.ipa
"$ADB" logcat -c
"$ADB" shell am start -n "$PKG/.ImportActivity" --es path "/data/data/$PKG/cache/import.ipa" >/dev/null
wait_log 'mcfm: engine started' 180
"$ADB" shell "run-as $PKG sh -c 'test -f files/game/minecraftpe.dylib && test -d files/game/data && ! test -e files/import && rm cache/import.ipa'" \
  || fail "the import did not leave files/game/{minecraftpe.dylib,data}"
wait_focus
shot title
tap 0.5 0.493     # Play
sleep 3
tap 0.5 0.21      # Create New World
sleep 3
tap 0.746 0.663   # Create World!
shot world --world
"$ADB" shell input keyevent KEYCODE_HOME
wait_log 'mcfm: paused (saved)' 30
"$ADB" shell "run-as $PKG sh -c 'ls files/home/games/com.mojang/minecraftWorlds/*/level.dat && test -f files/home/games/com.mojang/minecraftpe/options.txt'" >/dev/null \
  || fail "no world (level.dat) or options.txt saved under files/home/games/com.mojang"
"$ADB" logcat -c
"$ADB" shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$PKG/.ImportActivity" >/dev/null
wait_focus
sleep 3
tap 0.25 0.133    # Back to Game (the game pauses itself in the background)
shot resumed --world
crashed && fail "the app crashed after resuming"
pid="$("$ADB" shell pidof "$PKG" | tr -d '\r')"
[ -n "$pid" ] || fail "the app is not running"
echo "app_check: unsupported calls: $(log | grep -c -e 'unsupported' || true)"
log | grep 'unsupported' | sed 's/^/  /' | head -10 || true
echo "app_check: passed ($OUT/app-{title,world,resumed}.raw)"
