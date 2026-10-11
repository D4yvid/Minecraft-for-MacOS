#!/bin/bash
# usage: build_app.sh [--debug] --bundle <minecraftpe.ipa> <out.apk>   (make android-app /
# android-app-debug; the variables come from the Makefile).
# --bundle (required): the IPA's game (minecraftpe2 and data/, Mojang's files: the APK is a local
# build, never committed) goes in assets/game/; the app prepares it from itself on first start.
# --debug: debuggable (run-as) for the checks.
# Our Android app (docs/LAUNCHER.md, Stage 3c) without Gradle: aapt2 (resources, manifest),
# kotlinc (android/app/kotlin), d8 (with kotlin-stdlib), the native libraries stored uncompressed
# in lib/arm64-v8a, zipalign -P 16 (16 KB pages), apksigner with the local debug key.
# Needs: BUILD_TOOLS ANDROID_JAR KOTLINC JAVA_HOME LAUNCHER_SO STUBS_DIR (libmcfm_stub*.so, libmcfm_stubrt.so).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEBUG=()
BUNDLE=""
while [ $# -gt 1 ]; do
  case "$1" in
    --debug) DEBUG=(--debug-mode); shift ;;
    --bundle) BUNDLE="$2"; [ -f "$BUNDLE" ] || { echo "build_app: no IPA at $BUNDLE" >&2; exit 2; }; shift 2 ;;
    *) break ;;
  esac
done
OUT="$1"
[ -n "$BUNDLE" ] || { echo "build_app: the game's IPA is required (--bundle <minecraftpe.ipa>)" >&2; exit 2; }
: "${BUILD_TOOLS:?}" "${ANDROID_JAR:?}" "${KOTLINC:?}" "${JAVA_HOME:?}" "${LAUNCHER_SO:?}" "${STUBS_DIR:?}"
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
W="$ROOT/build/android-app${DEBUG[@]+-debug}"
rm -rf "$W" && mkdir -p "$W/res" "$W/dex" "$(dirname "$OUT")"
run() { local out; out="$("$@" 2>&1)" || { echo "build_app: $(basename "$1") failed:" >&2; echo "$out" >&2; exit 1; }; }

run "$BUILD_TOOLS/aapt2" compile --dir "$ROOT/android/app/res" -o "$W/res/compiled.zip"
run "$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" --manifest "$ROOT/android/app/AndroidManifest.xml" \
  --min-sdk-version 28 --target-sdk-version 37 --version-code 1 --version-name 0.1 ${DEBUG[@]+"${DEBUG[@]}"} \
  -o "$W/base.apk" "$W/res/compiled.zip"
run "$KOTLINC" -no-reflect -jvm-target 11 -cp "$ANDROID_JAR" -d "$W/classes.jar" $(find "$ROOT/android/app/kotlin" -name '*.kt' | sort)
STDLIB="$(dirname "$KOTLINC")/../lib/kotlin-stdlib.jar"
run "$BUILD_TOOLS/d8" --release --min-api 28 --lib "$ANDROID_JAR" --output "$W/dex" "$W/classes.jar" "$STDLIB"

# The APK: aapt2's output + classes.dex (deflated) + the native libraries (stored: loaded in place).
# The game: assets/game/{minecraftpe2,data/**} from the IPA, and assets/game/bundle.id (the IPA's
# SHA-256: the app prepares the game again when an update brings another one).
BUNDLE="$BUNDLE" python3 -I - "$W/base.apk" "$W/dex/classes.dex" "$W/unaligned.apk" "$LAUNCHER_SO" "$STUBS_DIR"/*.so <<'PY'
import hashlib, os, sys, zipfile
base, dex, out, libs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
bundle = os.environ['BUNDLE']
with zipfile.ZipFile(base) as src, zipfile.ZipFile(out, 'w') as dst:
    for item in src.infolist():
        dst.writestr(item, src.read(item.filename))
    dst.write(dex, 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
    for lib in libs:
        dst.write(lib, 'lib/arm64-v8a/' + os.path.basename(lib), compress_type=zipfile.ZIP_STORED)
    binary = 0
    with zipfile.ZipFile(bundle) as ipa:
        for e in ipa.infolist():
            parts = e.filename.split('/')
            if len(parts) < 3 or parts[0] != 'Payload' or not parts[1].endswith('.app') or e.is_dir():
                continue
            rest = parts[2:]
            if '..' in rest or not (rest == ['minecraftpe2'] or (len(rest) > 1 and rest[0] == 'data')):
                continue
            binary += rest == ['minecraftpe2']
            dst.writestr(zipfile.ZipInfo('assets/game/' + '/'.join(rest), e.date_time), ipa.read(e), zipfile.ZIP_DEFLATED)
    if binary != 1:
        sys.exit('build_app: %s has no Payload/*.app/minecraftpe2' % bundle)
    digest = hashlib.sha256(open(bundle, 'rb').read()).hexdigest()
    dst.writestr('assets/game/bundle.id', digest + '\n', zipfile.ZIP_DEFLATED)
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
