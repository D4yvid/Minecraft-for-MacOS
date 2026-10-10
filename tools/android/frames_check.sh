#!/bin/bash
# usage: frames_check.sh <dist/launcher> <android launcher out dir>   (make android-frames-check)
# Stage 3b acceptance (docs/LAUNCHER.md): on the running emulator/device the game boots with our
# AppPlatform (shared/launcher), renders 120 frames into a GLES 3 pbuffer, saves on suspend and
# leaves a screenshot of the last frame: <out>/android-shot.ppm (and .png). The game's data/
# directory is pushed once (re-pushed when it changes).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIST="$1"; OUT="$2"
SDK="${ANDROID_SDK:-$HOME/Library/Android/sdk}"
ADB="$SDK/platform-tools/adb"
DIR=/data/local/tmp/mcfm
IMAGE="$DIST/libminecraftpe.dylib"
[ -f "$IMAGE" ] && [ -f "$DIST/imports.tsv" ] && [ -f "$DIST/data_dir.txt" ] \
  || { echo "frames_check: no converted game in $DIST (make app GAME=…)" >&2; exit 2; }
DATA="$(cat "$DIST/data_dir.txt")"
[ -d "$DATA" ] || { echo "frames_check: game data not found: $DATA" >&2; exit 2; }
ANDROID_CC="${ANDROID_CC:?set ANDROID_CC}" bash "$ROOT/tools/launcher/build_stubs.sh" --target android "$DIST/imports.tsv" "$OUT/stubs" >/dev/null

STAMP="v2-$(cd "$DATA" && find . -type f -exec stat -f '%N %z %m' {} + | sort | shasum | awk '{print $1}')"
if [ "$("$ADB" shell cat "$DIR/data/.mcfm-stamp" 2>/dev/null | tr -d '\r')" != "$STAMP" ]; then
  echo "frames_check: pushing data/ ($(du -sh "$DATA" | awk '{print $1}'))"
  T="$(mktemp -d)"
  # No AppleDouble (._*) files or extended attributes; the device extracts without owners
  # (toybox tar would try to chown to the Mac's uid, which fails on Android 9).
  COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs -C "$DATA" -cf "$T/data.tar" .
  "$ADB" shell "rm -rf $DIR/data && mkdir -p $DIR/data" >/dev/null
  "$ADB" push "$T/data.tar" "$DIR/data.tar" >/dev/null 2>&1
  "$ADB" shell "cd $DIR/data && tar -xof ../data.tar && rm ../data.tar && echo $STAMP > .mcfm-stamp" \
    || { echo "frames_check: cannot unpack data/ on the device" >&2; exit 1; }
  rm -rf "$T"
fi
"$ADB" shell "rm -rf $DIR/home $DIR/shot.ppm" >/dev/null

PUSH=(--push "$OUT/libmcfm_launcher.so" --push "$IMAGE")
for so in "$OUT"/stubs/*.so; do PUSH+=(--push "$so"); done
set +e
OUTPUT="$(bash "$ROOT/tools/android/adb_run.sh" "${PUSH[@]}" "$OUT/mcfm-run" @DIR@/libminecraftpe.dylib --boot \
  --data @DIR@/data/ --home @DIR@/home --frames 120 --screenshot @DIR@/shot.ppm 2>&1)"
CODE=$?
set -e
printf '%s\n' "$OUTPUT" | grep -v '^mcfm: stub ' | tail -16
echo "frames_check: stubs called: $(grep -c '^mcfm: stub ' <<<"$OUTPUT" || true)"
grep '^mcfm: stub ' <<<"$OUTPUT" | sed 's/^/  /' | head -40
[ $CODE = 0 ] || { echo "frames_check: FAIL (mcfm-run exited $CODE)"; exit 1; }
grep -q '^mcfm: engine started (1280x720)$' <<<"$OUTPUT" || { echo "frames_check: FAIL (engine did not start)"; exit 1; }
grep -q '^mcfm: 120 frames rendered$' <<<"$OUTPUT" || { echo "frames_check: FAIL (120 frames not rendered)"; exit 1; }
grep -q '^mcfm: audio: output started' <<<"$OUTPUT" || { echo "frames_check: FAIL (FMOD's audio output did not start)"; exit 1; }
"$ADB" pull "$DIR/shot.ppm" "$OUT/android-shot.ppm" >/dev/null 2>&1 || { echo "frames_check: FAIL (no screenshot)"; exit 1; }
sips -s format png "$OUT/android-shot.ppm" --out "$OUT/android-shot.png" >/dev/null 2>&1 || true
python3 -I "$ROOT/tools/android/check_screenshot.py" "$OUT/android-shot.ppm" || { echo "frames_check: FAIL (screenshot)"; exit 1; }
"$ADB" shell "test -f $DIR/home/games/com.mojang/minecraftpe/options.txt" \
  || { echo "frames_check: FAIL (no options.txt saved under home/games/com.mojang/minecraftpe)"; exit 1; }
echo "frames_check: passed ($OUT/android-shot.png)"
