#!/bin/bash
# usage: app_run.sh <mcfm-debug.apk> <api 37|28>   (make android-app-run)
# Plays the app in the emulator, in a window with sound: boots it (a headless one is restarted),
# installs the APK keeping the app's data (worlds, the prepared game) and opens the game.
# Logs: adb logcat -s mcfm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APK="$1"; API="$2"
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="$SDK/platform-tools/adb"
PKG=io.github.d4yvid.mcfm
ANDROID_SDK="$SDK" bash "$ROOT/tools/android/emulator.sh" window "$API"
"$ADB" install -r "$APK" >/dev/null || { echo "app_run: install failed (another signature? adb uninstall $PKG)" >&2; exit 1; }
"$ADB" shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$PKG/.GameActivity" >/dev/null
echo "app_run: the game is in the emulator's window (logs: $ADB logcat -s mcfm)"
