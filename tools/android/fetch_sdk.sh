#!/bin/bash
# usage: fetch_sdk.sh [sdk dir]   (default: ~/Library/Android/sdk)
# The arm64 Android toolchain for the launcher (docs/LAUNCHER.md, Stage 3): NDK r27d,
# platform-tools (adb), build-tools 37 and the API 37 platform (the app), the emulator and two
# arm64 system images (API 37 with 16 KB pages, the
# target; API 28, the oldest supported), straight from Google's
# SDK repository (no sdkmanager, so no Java), each verified by the SHA-1 the repository
# publishes. Packages already present are kept. Nothing goes into the repository.
set -euo pipefail
SDK="${1:-$HOME/Library/Android/sdk}"
BASE="${FETCH_SDK_BASE:-https://dl.google.com/android/repository}"
# path in the SDK | archive | sha1 | top-level directory in the archive
PACKAGES=(
  "ndk/27.3.13750724|android-ndk-r27d-darwin.zip|2970926d705988f79baa9b04c51b4f7914dd8c56|android-ndk-r27d"
  "platform-tools|platform-tools_r37.0.1-darwin.zip|6ae73f4de6452dc57e62ec02b68eed92a4c21661|platform-tools"
  "emulator|emulator-darwin_aarch64-16489710.zip|bc90f6bcacf23a6a8a4fda40fd0bba37b99d9582|emulator"
  "system-images/android-28/default/arm64-v8a|sys-img/android/arm64-v8a-28_r02.zip|e209114dd0dfc2f4e0d328f5fd7367fec39ee1bd|arm64-v8a"
  "build-tools/37.0.0|build-tools_r37_macosx.zip|eb080751b2b2028eb3604f571027d6f7b3c46321|android-37.0"
  "platforms/android-37.0|platform-37.0_r02.zip|ed8ebf7f8822a4de5686d427f237d2fa30ff7410|android-37.0"
  "system-images/android-37.0/google_apis_ps16k/arm64-v8a|sys-img/google_apis/arm64-v8a-ps16k-37.0_r07.zip|a661370122e12de2a9d81da44c838511b3c89277|arm64-v8a"
)
# Tests replace the list (space-separated entries) and the base URL.
if [ -n "${FETCH_SDK_PACKAGES:-}" ]; then read -r -a PACKAGES <<<"$FETCH_SDK_PACKAGES"; fi
mkdir -p "$SDK"
for p in "${PACKAGES[@]}"; do
  IFS='|' read -r dest archive sha top <<<"$p"
  if [ -e "$SDK/$dest" ]; then echo "fetch_sdk: $dest present"; continue; fi
  TMP="$(mktemp -d "$SDK/.fetch.XXXXXX")"
  echo "fetch_sdk: downloading $archive"
  curl --http1.1 --retry 3 -fsSL -o "$TMP/a.zip" "$BASE/$archive" || { rm -rf "$TMP"; echo "fetch_sdk: cannot download $archive" >&2; exit 1; }
  GOT="$(shasum -a 1 "$TMP/a.zip" | awk '{print $1}')"
  [ "$GOT" = "$sha" ] || { rm -rf "$TMP"; echo "fetch_sdk: $archive checksum mismatch ($GOT, expected $sha)" >&2; exit 1; }
  unzip -q "$TMP/a.zip" -d "$TMP/x" || { rm -rf "$TMP"; echo "fetch_sdk: $archive does not unpack" >&2; exit 1; }
  [ -d "$TMP/x/$top" ] || { rm -rf "$TMP"; echo "fetch_sdk: $archive has no $top/" >&2; exit 1; }
  mkdir -p "$(dirname "$SDK/$dest")"
  mv "$TMP/x/$top" "$SDK/$dest"
  rm -rf "$TMP"
  echo "fetch_sdk: $dest ready"
done
