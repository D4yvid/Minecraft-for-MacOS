#!/bin/bash
# usage: emulator.sh start|window|stop|status [37|28]   (SDK: $ANDROID_SDK, default ~/Library/Android/sdk)
# The arm64 emulators the Stage 3 tests run on (docs/LAUNCHER.md): API 37 with 16 KB pages (the
# target, default) or API 28 (the oldest supported). Creates the AVD ("mcfm37"/"mcfm28") by hand
# (avdmanager needs Java), boots it and waits for boot: `start` headless (the checks), `window`
# in a window with sound, to play (a running headless one is restarted; a windowed one is kept by
# `start` too). One runs at a time. Runs natively on Apple Silicon (arm64 guest,
# Hypervisor.framework).
set -euo pipefail
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="$SDK/platform-tools/adb"
AVD_HOME="$HOME/.android/avd"
API="${2:-37}"
case "$API" in
  37) NAME=mcfm37; IMAGE=system-images/android-37.0/google_apis_ps16k/arm64-v8a; TAG=google_apis_ps16k ;;
  28) NAME=mcfm28; IMAGE=system-images/android-28/default/arm64-v8a; TAG=default ;;
  *) echo "emulator: API 37 or 28" >&2; exit 2 ;;
esac
export ANDROID_SDK_ROOT="$SDK" ANDROID_AVD_HOME="$AVD_HOME"
MODE_FILE="${TMPDIR:-/tmp}/mcfm-emulator.mode"  # how the running one was started: headless|window
running() { "$ADB" devices | grep -q '^emulator-'; }
stop() {
  running || return 0
  "$ADB" emu kill >/dev/null 2>&1 || true
  for _ in $(seq 1 60); do running || { rm -f "$MODE_FILE"; return 0; }; sleep 1; done
  echo "emulator: did not stop" >&2; exit 1
}
boot() {  # boot <headless|window>
  create_avd
  local flags=(-no-snapshot -no-boot-anim)
  [ "$1" = headless ] && flags+=(-no-window -no-audio)
  nohup "$SDK/emulator/emulator" -avd "$NAME" "${flags[@]}" > "${TMPDIR:-/tmp}/mcfm-emulator.log" 2>&1 &
  echo "$1" > "$MODE_FILE"
  "$ADB" wait-for-device
  until [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
  echo "emulator: $NAME booted ($1, $("$ADB" shell getprop ro.build.version.release | tr -d '\r'), $("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r'))"
}
# The API level of the running emulator (to switch when another one is asked for).
running_api() { "$ADB" shell getprop ro.build.version.sdk 2>/dev/null | tr -d '\r'; }

create_avd() {
  [ -f "$AVD_HOME/$NAME.ini" ] && return
  [ -d "$SDK/$IMAGE" ] || { echo "emulator: no $IMAGE; run make android-sdk" >&2; exit 1; }
  mkdir -p "$AVD_HOME/$NAME.avd" "$SDK/platforms"
  printf 'avd.ini.encoding=UTF-8\npath=%s\npath.rel=avd/%s.avd\ntarget=android-%s\n' "$AVD_HOME/$NAME.avd" "$NAME" "$API" > "$AVD_HOME/$NAME.ini"
  cat > "$AVD_HOME/$NAME.avd/config.ini" <<EOF
AvdId=$NAME
avd.ini.displayname=$NAME
abi.type=arm64-v8a
hw.cpu.arch=arm64
hw.cpu.ncore=4
hw.ramSize=3072
disk.dataPartition.size=6G
image.sysdir.1=$IMAGE/
tag.id=$TAG
hw.gpu.enabled=yes
hw.gpu.mode=host
hw.lcd.width=1280
hw.lcd.height=720
hw.lcd.density=240
hw.keyboard=yes
fastboot.forceColdBoot=yes
EOF
}

case "${1:-}" in
  start)
    if running; then echo "emulator: already running ($(cat "$MODE_FILE" 2>/dev/null || echo started elsewhere))"; exit 0; fi
    boot headless
    ;;
  window)
    if running && [ "$(cat "$MODE_FILE" 2>/dev/null)" = window ] && [ "$(running_api)" = "$([ "$API" = 37 ] && echo 37 || echo 28)" ]; then
      echo "emulator: $NAME already running in a window"; exit 0
    fi
    stop
    boot window
    ;;
  stop) stop ;;
  status) "$ADB" devices ;;
  *) echo "usage: emulator.sh start|window|stop|status [37|28]" >&2; exit 2 ;;
esac
