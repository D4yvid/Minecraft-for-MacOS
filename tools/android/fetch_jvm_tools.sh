#!/bin/bash
# usage: fetch_jvm_tools.sh [dir]   (default: ~/Library/Android/jvm-tools)
# The JVM tools the Android app's build needs (docs/LAUNCHER.md, Stage 3c; no Gradle): a JDK
# (Eclipse Temurin 21, arm64 macOS) for d8/apksigner/keytool and the Kotlin compiler, from
# their official releases, verified by SHA-256 (Adoptium's and JetBrains' published sums).
# Unpacked as <dir>/jdk-21 and <dir>/kotlinc; tools already present are kept.
set -euo pipefail
OUT="${1:-$HOME/Library/Android/jvm-tools}"
# name | url | sha256 | top-level directory in the archive
TOOLS=(
  "jdk-21|https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.12.1_1.tar.gz|3623232f33a9c3baadf304480b2535f9a3cba8a58d42ecbb438ba267315d9998|jdk-21.0.12.1+1"
  "kotlinc|https://github.com/JetBrains/kotlin/releases/download/v2.4.21/kotlin-compiler-2.4.21.zip|7cc140e76daf416a0424a5557f99afd7ca89ebd463ea1101261e3e01c33e75cd|kotlinc"
)
# Tests replace the list (space-separated entries).
if [ -n "${FETCH_JVM_TOOLS:-}" ]; then read -r -a TOOLS <<<"$FETCH_JVM_TOOLS"; fi
mkdir -p "$OUT"
for t in "${TOOLS[@]}"; do
  IFS='|' read -r name url sha top <<<"$t"
  if [ -e "$OUT/$name" ]; then echo "fetch_jvm_tools: $name present"; continue; fi
  TMP="$(mktemp -d "$OUT/.fetch.XXXXXX")"
  echo "fetch_jvm_tools: downloading $name"
  curl --http1.1 --retry 3 -fsSL -o "$TMP/archive" "$url" || { rm -rf "$TMP"; echo "fetch_jvm_tools: cannot download $name" >&2; exit 1; }
  GOT="$(shasum -a 256 "$TMP/archive" | awk '{print $1}')"
  [ "$GOT" = "$sha" ] || { rm -rf "$TMP"; echo "fetch_jvm_tools: $name checksum mismatch ($GOT, expected $sha)" >&2; exit 1; }
  mkdir "$TMP/x"
  case "$url" in
    *.zip) unzip -q "$TMP/archive" -d "$TMP/x" ;;
    *) tar xzf "$TMP/archive" -C "$TMP/x" ;;
  esac || { rm -rf "$TMP"; echo "fetch_jvm_tools: $name does not unpack" >&2; exit 1; }
  [ -d "$TMP/x/$top" ] || { rm -rf "$TMP"; echo "fetch_jvm_tools: $name has no $top/" >&2; exit 1; }
  mv "$TMP/x/$top" "$OUT/$name"
  rm -rf "$TMP"
  echo "fetch_jvm_tools: $name ready"
done
