#!/bin/bash
# fetch_sdk.sh / fetch_llvm_runtimes.sh / fetch_jvm_tools.sh: unpack only archives whose checksum matches and that
# hold the expected directory; keep what is present. Local stand-in archives (no network).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; mkdir -p "$T"; trap 'rm -rf "$(dirname "$T")"' EXIT
BASE="file://${T// /%20}"
fails=0
# SDK: a zip with top directory "pkg".
mkdir -p "$T/z/pkg" && echo hi > "$T/z/pkg/file"
(cd "$T/z" && zip -qr "$T/good.zip" pkg)
mkdir -p "$T/w/other" && echo x > "$T/w/other/file"
(cd "$T/w" && zip -qr "$T/wrongtop.zip" other)
SHA="$(shasum -a 1 "$T/good.zip" | awk '{print $1}')"
WSHA="$(shasum -a 1 "$T/wrongtop.zip" | awk '{print $1}')"
sdk() { FETCH_SDK_BASE="$BASE" FETCH_SDK_PACKAGES="$1" bash "$ROOT/tools/android/fetch_sdk.sh" "$2"; }
sdk "a/b/pkg|good.zip|$SHA|pkg" "$T/sdk1" >/dev/null || { echo "FAIL: good zip refused"; fails=$((fails+1)); }
[ "$(cat "$T/sdk1/a/b/pkg/file" 2>/dev/null)" = hi ] || { echo "FAIL: good zip not unpacked under its path"; fails=$((fails+1)); }
sdk "pkg|good.zip|0000|pkg" "$T/sdk2" >/dev/null 2>&1 && { echo "FAIL: wrong SHA-1 accepted"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/sdk2" 2>/dev/null)" ] || { echo "FAIL: files left after a checksum failure"; fails=$((fails+1)); }
sdk "pkg|wrongtop.zip|$WSHA|pkg" "$T/sdk3" >/dev/null 2>&1 && { echo "FAIL: zip without its directory accepted"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/sdk3" 2>/dev/null)" ] || { echo "FAIL: files left after a bad archive"; fails=$((fails+1)); }
echo changed > "$T/sdk1/a/b/pkg/file"
sdk "a/b/pkg|missing.zip|$SHA|pkg" "$T/sdk1" >/dev/null || { echo "FAIL: present package not kept"; fails=$((fails+1)); }
[ "$(cat "$T/sdk1/a/b/pkg/file")" = changed ] || { echo "FAIL: present package replaced"; fails=$((fails+1)); }
# LLVM: <name>-18.1.8.src.tar.xz with top directory <name>-18.1.8.src.
mkdir -p "$T/l/libfoo-18.1.8.src" && echo src > "$T/l/libfoo-18.1.8.src/f"
(cd "$T/l" && tar cJf "$T/libfoo-18.1.8.src.tar.xz" libfoo-18.1.8.src)
mkdir -p "$T/l2/nope" && (cd "$T/l2" && tar cJf "$T/libbar-18.1.8.src.tar.xz" nope)
LSHA="$(shasum -a 256 "$T/libfoo-18.1.8.src.tar.xz" | awk '{print $1}')"
BSHA="$(shasum -a 256 "$T/libbar-18.1.8.src.tar.xz" | awk '{print $1}')"
llvm() { FETCH_LLVM_BASE="$BASE" FETCH_LLVM_TARBALLS="$1" bash "$ROOT/tools/android/fetch_llvm_runtimes.sh" "$2"; }
llvm "libfoo|$LSHA" "$T/llvm1" >/dev/null || { echo "FAIL: good tarball refused"; fails=$((fails+1)); }
[ "$(cat "$T/llvm1/libfoo/f" 2>/dev/null)" = src ] || { echo "FAIL: tarball not unpacked"; fails=$((fails+1)); }
llvm "libfoo|0000" "$T/llvm2" >/dev/null 2>&1 && { echo "FAIL: wrong SHA-256 accepted"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/llvm2" 2>/dev/null)" ] || { echo "FAIL: files left after a checksum failure (llvm)"; fails=$((fails+1)); }
llvm "libbar|$BSHA" "$T/llvm3" >/dev/null 2>&1 && { echo "FAIL: tarball without its directory accepted"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/llvm3" 2>/dev/null)" ] || { echo "FAIL: files left after a bad tarball"; fails=$((fails+1)); }
# JVM tools (Stage 3c): tar.gz and zip archives with SHA-256, unpacked under their name.
mkdir -p "$T/j/jdk-x/bin" && echo java > "$T/j/jdk-x/bin/java"
(cd "$T/j" && tar czf "$T/jdk.tar.gz" jdk-x)
mkdir -p "$T/k/kotlinc/bin" && echo kotlinc > "$T/k/kotlinc/bin/kotlinc"
(cd "$T/k" && zip -qr "$T/kotlin.zip" kotlinc)
JSHA="$(shasum -a 256 "$T/jdk.tar.gz" | awk '{print $1}')"
KSHA="$(shasum -a 256 "$T/kotlin.zip" | awk '{print $1}')"
jvm() { FETCH_JVM_TOOLS="$1" bash "$ROOT/tools/android/fetch_jvm_tools.sh" "$2"; }
jvm "jdk-21|$BASE/jdk.tar.gz|$JSHA|jdk-x kotlinc|$BASE/kotlin.zip|$KSHA|kotlinc" "$T/jvm1" >/dev/null \
  || { echo "FAIL: good JVM tools refused"; fails=$((fails+1)); }
[ "$(cat "$T/jvm1/jdk-21/bin/java" 2>/dev/null)" = java ] && [ "$(cat "$T/jvm1/kotlinc/bin/kotlinc" 2>/dev/null)" = kotlinc ] \
  || { echo "FAIL: JVM tools not unpacked under their names"; fails=$((fails+1)); }
jvm "jdk-21|$BASE/jdk.tar.gz|0000|jdk-x" "$T/jvm2" >/dev/null 2>&1 && { echo "FAIL: wrong SHA-256 accepted (jvm)"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/jvm2" 2>/dev/null)" ] || { echo "FAIL: files left after a checksum failure (jvm)"; fails=$((fails+1)); }
jvm "kotlinc|$BASE/kotlin.zip|$KSHA|wrong" "$T/jvm3" >/dev/null 2>&1 && { echo "FAIL: archive without its directory accepted (jvm)"; fails=$((fails+1)); }
[ -z "$(ls -A "$T/jvm3" 2>/dev/null)" ] || { echo "FAIL: files left after a bad archive (jvm)"; fails=$((fails+1)); }
echo changed > "$T/jvm1/kotlinc/bin/kotlinc"
jvm "kotlinc|$BASE/missing.zip|$KSHA|kotlinc" "$T/jvm1" >/dev/null || { echo "FAIL: present JVM tool not kept"; fails=$((fails+1)); }
[ "$(cat "$T/jvm1/kotlinc/bin/kotlinc")" = changed ] || { echo "FAIL: present JVM tool replaced"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "fetch_sdk_test: passed" || { echo "$fails failure(s)"; exit 1; }
