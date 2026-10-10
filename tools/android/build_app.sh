#!/bin/bash
# usage: build_app.sh <out.apk>   (make android-app; the variables come from the Makefile)
# Our Android app (docs/LAUNCHER.md, Stage 3c) without Gradle: aapt2 (resources, manifest),
# kotlinc (android/app/kotlin), d8 (with kotlin-stdlib), the native libraries stored uncompressed
# in lib/arm64-v8a, zipalign -P 16 (16 KB pages), apksigner with the local debug key.
# Needs: BUILD_TOOLS ANDROID_JAR KOTLINC JAVA_HOME LAUNCHER_SO STUBS_DIR (libmcfm_stub*.so, libmcfm_stubrt.so).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"
: "${BUILD_TOOLS:?}" "${ANDROID_JAR:?}" "${KOTLINC:?}" "${JAVA_HOME:?}" "${LAUNCHER_SO:?}" "${STUBS_DIR:?}"
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
W="$ROOT/build/android-app"
rm -rf "$W" && mkdir -p "$W/res" "$W/dex" "$(dirname "$OUT")"
run() { local out; out="$("$@" 2>&1)" || { echo "build_app: $(basename "$1") failed:" >&2; echo "$out" >&2; exit 1; }; }

run "$BUILD_TOOLS/aapt2" compile --dir "$ROOT/android/app/res" -o "$W/res/compiled.zip"
run "$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" --manifest "$ROOT/android/app/AndroidManifest.xml" \
  --min-sdk-version 28 --target-sdk-version 37 --version-code 1 --version-name 0.1 --debug-mode \
  -o "$W/base.apk" "$W/res/compiled.zip"
run "$KOTLINC" -no-reflect -jvm-target 11 -cp "$ANDROID_JAR" -d "$W/classes.jar" $(find "$ROOT/android/app/kotlin" -name '*.kt' | sort)
STDLIB="$(dirname "$KOTLINC")/../lib/kotlin-stdlib.jar"
run "$BUILD_TOOLS/d8" --release --min-api 28 --lib "$ANDROID_JAR" --output "$W/dex" "$W/classes.jar" "$STDLIB"

# The APK: aapt2's output + classes.dex (deflated) + the native libraries (stored: loaded in place).
python3 -I - "$W/base.apk" "$W/dex/classes.dex" "$W/unaligned.apk" "$LAUNCHER_SO" "$STUBS_DIR"/*.so <<'PY'
import sys, zipfile, os
base, dex, out, libs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
with zipfile.ZipFile(base) as src, zipfile.ZipFile(out, 'w') as dst:
    for item in src.infolist():
        dst.writestr(item, src.read(item.filename))
    dst.write(dex, 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
    for lib in libs:
        dst.write(lib, 'lib/arm64-v8a/' + os.path.basename(lib), compress_type=zipfile.ZIP_STORED)
PY
run "$BUILD_TOOLS/zipalign" -f -P 16 4 "$W/unaligned.apk" "$W/aligned.apk"
KEYSTORE="${MCFM_KEYSTORE:-$ROOT/build/android/debug.keystore}"
if [ ! -f "$KEYSTORE" ]; then
  mkdir -p "$(dirname "$KEYSTORE")"
  run keytool -genkeypair -keystore "$KEYSTORE" -alias mcfm -storepass android -keypass android \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=mcfm debug"
fi
run "$BUILD_TOOLS/apksigner" sign --ks "$KEYSTORE" --ks-key-alias mcfm --ks-pass pass:android --key-pass pass:android \
  --out "$OUT" "$W/aligned.apk"
echo "build_app: $OUT"
