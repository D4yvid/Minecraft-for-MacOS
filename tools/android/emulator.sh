#!/bin/bash
# usage: emulator.sh start|stop|status [37|28]   (SDK: $ANDROID_SDK, default ~/Library/Android/sdk)
# The arm64 emulators the Stage 3 tests run on (docs/LAUNCHER.md): API 37 with 16 KB pages (the
# target, default) or API 28 (the oldest supported). Creates the AVD ("mcfm37"/"mcfm28") by hand
# (avdmanager needs Java), boots it headless and waits for boot. One runs at a time. Runs
# natively on Apple Silicon (arm64 guest, Hypervisor.framework).
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
    create_avd
    if "$ADB" devices | grep -q '^emulator-'; then echo "emulator: already running"; exit 0; fi
    nohup "$SDK/emulator/emulator" -avd "$NAME" -no-window -no-audio -no-snapshot -no-boot-anim \
      > "${TMPDIR:-/tmp}/mcfm-emulator.log" 2>&1 &
    "$ADB" wait-for-device
    until [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
    echo "emulator: $NAME booted ($("$ADB" shell getprop ro.build.version.release | tr -d '\r'), $("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r'))"
    ;;
  stop) "$ADB" emu kill >/dev/null 2>&1 || true ;;
  status) "$ADB" devices ;;
  *) echo "usage: emulator.sh start|stop|status" >&2; exit 2 ;;
esac
