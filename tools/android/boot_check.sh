#!/bin/bash
# usage: boot_check.sh <dist/launcher> <android launcher out dir>   (make android-boot-check)
# Stage 3a acceptance (docs/LAUNCHER.md): the converted game image (from make app) loads on the
# running emulator/device with our loader, the Darwin layer and the Apple-ABI runtime, and every
# one of its static initializers runs. Builds the Android stubs from the image's imports.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIST="$1"; OUT="$2"
IMAGE="$DIST/libminecraftpe.dylib"
[ -f "$IMAGE" ] && [ -f "$DIST/imports.tsv" ] || { echo "boot_check: no converted game in $DIST (make app GAME=…)" >&2; exit 2; }
ANDROID_CC="${ANDROID_CC:?set ANDROID_CC}" bash "$ROOT/tools/launcher/build_stubs.sh" --target android "$DIST/imports.tsv" "$OUT/stubs" >/dev/null
# Expected: every pointer in __mod_init_func plus every offset in __init_offsets.
EXPECTED=0
while read -r unit size; do EXPECTED=$((EXPECTED + size / unit)); done < <(otool -l "$IMAGE" | awk '
  /sectname __mod_init_func/ {k=8} /sectname __init_offsets/ {k=4}
  k && $1=="size" {print k, $2; k=0}')
PUSH=(--push "$OUT/libmcfm_launcher.so" --push "$IMAGE")
for so in "$OUT"/stubs/*.so; do PUSH+=(--push "$so"); done
echo "boot_check: loading the game on $("${ANDROID_SDK:-$HOME/Library/Android/sdk}/platform-tools/adb" shell getprop ro.build.version.release | tr -d '\r') ($EXPECTED initializers expected)"
set +e
OUTPUT="$(bash "$ROOT/tools/android/adb_run.sh" "${PUSH[@]}" "$OUT/mcfm-run" @DIR@/libminecraftpe.dylib --initializers-only 2>&1)"
CODE=$?
set -e
printf '%s\n' "$OUTPUT" | grep -v '^mcfm: stub ' | tail -20
STUBS="$(grep -c '^mcfm: stub ' <<<"$OUTPUT" || true)"
echo "boot_check: $STUBS distinct stubs called"
[ $CODE = 0 ] || { echo "boot_check: FAIL (mcfm-run exited $CODE)"; exit 1; }
grep -qx "mcfm: $EXPECTED initializers ran" <<<"$OUTPUT" || { echo "boot_check: FAIL (not all $EXPECTED initializers ran)"; exit 1; }
echo "boot_check: passed"
