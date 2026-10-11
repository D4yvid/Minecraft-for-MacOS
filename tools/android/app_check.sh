#!/bin/bash
# usage: app_check.sh <mcfm-debug.apk> <out dir>   (make android-app-check)
# Stage 3c acceptance (docs/LAUNCHER.md) on the running emulator/device, from a clean install of
# the app with its bundled game: the first start prepares the game, the title screen shows,
# Play -> Create New World -> Create World! reaches a world (touch input), the game saves when the
# app goes to the background and draws again when it comes back (twice, then the screen off and
# on). Then: an image whose hooks changed is converted again from the kept binary, and a damaged
# copy shows its message (no crash), "Close" ends the process and the next start prepares the game
# again. Screenshots: <out>/app-*.raw.
# The taps are fractions of the screen, measured on the game's GUI at 1280x720 (the emulators).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APK="$1"; OUT="$2"
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="$SDK/platform-tools/adb"
PKG=io.github.d4yvid.mcfm
[ -f "$APK" ] || { echo "app_check: no APK at $APK (make android-app)" >&2; exit 2; }
mkdir -p "$OUT"
fail() { echo "app_check: FAIL ($1)"; "$ADB" logcat -d -s mcfm:I AndroidRuntime:E DEBUG:F libc:F | grep -v 'mcfm: stub ' | tail -30; exit 1; }
log() { "$ADB" logcat -d -s mcfm:I AndroidRuntime:E DEBUG:F libc:F 2>/dev/null; }
crashed() { log | grep -q -e 'FATAL EXCEPTION' -e 'Fatal signal' -e '>>> io.github.d4yvid.mcfm' -e 'mcfm: terminate' -e 'DetachCurrentThread'; }
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
wait_focus() {  # [activity, default GameActivity]
  for _ in $(seq 1 30); do
    "$ADB" shell dumpsys window | grep -q "mCurrentFocus=.*$PKG/$PKG.${1:-GameActivity}" && return 0
    crashed && fail "the app crashed before ${1:-the game} came to the front"
    sleep 1
  done
  fail "${1:-the game} did not come to the front"
}
launch() { "$ADB" shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$PKG/.GameActivity" >/dev/null; }
# ui_tap <text>: taps the view showing <text> (uiautomator; for the app's own dialogs).
ui_tap() {
  "$ADB" shell uiautomator dump /data/local/tmp/mcfm-ui.xml >/dev/null 2>&1
  local xy
  xy="$("$ADB" shell cat /data/local/tmp/mcfm-ui.xml | python3 -I -c '
import re, sys
for m in re.finditer(r"<node [^>]*>", sys.stdin.read()):
    n = m.group(0)
    if re.search(r"text=\"%s\"" % re.escape(sys.argv[1]), n, re.I):
        x0, y0, x1, y1 = map(int, re.search(r"bounds=\"\[(\d+),(\d+)\]\[(\d+),(\d+)\]\"", n).groups())
        print((x0 + x1) // 2, (y0 + y1) // 2); break' "$1")"
  [ -n "$xy" ] || return 1
  "$ADB" shell input tap $xy
}
# cycle <name> <keyevents...>: the app leaves the screen and comes back; the game saved and draws.
cycle() {
  local name="$1"; shift
  "$ADB" logcat -c
  for k in "$@"; do "$ADB" shell input keyevent "$k"; sleep 2; done
  case "$name" in home*) launch ;; esac
  log | grep -q 'mcfm: paused (saved)' || fail "no save when the app left the screen ($name)"
  wait_focus
  sleep 3
  tap 0.25 0.133    # Back to Game (the game pauses itself in the background)
  shot "$name" --world
}
read -r W H < <("$ADB" shell wm size | awk -F'[ x]' '/size/ {w=$(NF-1); h=$NF} END {print w, h}')
[ "$W" -lt "$H" ] && { t=$W; W=$H; H=$t; }  # the game runs in landscape
tap() { "$ADB" shell input tap "$(awk "BEGIN {print int($W * $1)}")" "$(awk "BEGIN {print int($H * $2)}")"; }

echo "app_check: installing $(basename "$APK") on $("$ADB" shell getprop ro.build.version.release | tr -d '\r') (${W}x${H})"
"$ADB" install -r "$APK" >/dev/null
"$ADB" shell "run-as $PKG true" 2>/dev/null || fail "$(basename "$APK") is not debuggable (make android-app-debug)"
"$ADB" shell pm clear "$PKG" >/dev/null  # a fresh install: no game, no worlds
"$ADB" shell am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS >/dev/null  # e.g. an old crash dialog
"$ADB" logcat -c
launch
wait_log 'mcfm: engine started' 180
"$ADB" shell "run-as $PKG sh -c 'cd files && test -f game/minecraftpe.dylib && test -f game/minecraftpe.hooks && test -f game/minecraftpe2 && test -d game/data && test -s game/bundle.id && ! ls -d import* game.old 2>/dev/null'" >/dev/null \
  || fail "the first start did not leave files/game/{minecraftpe.dylib,minecraftpe.hooks,minecraftpe2,data,bundle.id} alone"
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
launch
wait_focus
sleep 3
tap 0.25 0.133    # Back to Game (the game pauses itself in the background)
shot resumed --world
cycle home KEYCODE_HOME
cycle screen KEYCODE_POWER KEYCODE_WAKEUP KEYCODE_MENU
crashed && fail "the app crashed after resuming"
pid="$("$ADB" shell pidof "$PKG" | tr -d '\r')"
[ -n "$pid" ] || fail "the app is not running"
echo "app_check: unsupported calls: $(log | grep -c -e 'unsupported' || true)"
log | grep 'unsupported' | sed 's/^/  /' | head -10 || true

# An update that changes the launcher's hooks: the image is converted again, no new import.
"$ADB" shell am force-stop "$PKG"
"$ADB" shell "run-as $PKG sh -c 'echo stale > files/game/minecraftpe.hooks'"
"$ADB" logcat -c
launch
wait_log 'converting the game again' 60
wait_log 'mcfm: engine started' 120
wait_focus
shot reconverted

# A damaged copy (the hooks changed and the kept binary is broken): its message, no crash;
# "Close" ends the process, and the next start prepares the bundled game again.
"$ADB" shell am force-stop "$PKG"
"$ADB" shell "run-as $PKG sh -c 'echo broken > files/game/minecraftpe2 && echo stale > files/game/minecraftpe.hooks'"
"$ADB" logcat -c
launch
for i in $(seq 1 30); do ui_tap "Close" && break; [ "$i" = 30 ] && fail "no 'cannot start' dialog"; sleep 1; done
sleep 2
crashed && fail "the app crashed on a damaged game"
[ -z "$("$ADB" shell pidof "$PKG" | tr -d '\r')" ] || fail "the process is still running after the dialog"
"$ADB" logcat -c
launch
wait_log 'mcfm: engine started' 180
wait_focus
shot prepared-again
"$ADB" shell am force-stop "$PKG"
echo "app_check: passed ($OUT/app-{title,world,resumed,home,screen,reconverted,prepared-again}.raw)"
