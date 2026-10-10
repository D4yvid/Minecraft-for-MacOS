#!/bin/bash
# usage: fetch_llvm_runtimes.sh <outdir>
# LLVM 18.1.8 sources of libc++, libc++abi and libunwind (the NDK r27 compiler is LLVM 18): the
# Android launcher builds them with Apple's arm64 ABI settings for the game (docs/LAUNCHER.md,
# Stage 3). Official release tarballs, verified by SHA-256 (pinned on first download,
# 2026-10-10). Nothing is committed.
set -euo pipefail
OUT="$1"
BASE="${FETCH_LLVM_BASE:-https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8}"
TARBALLS=(
  "libcxx|bdecf90be0072bc720fd5c9c8ab061cdb197edd0c8ad3e170dc3e6bfaa49f388"
  "libcxxabi|256c30d724eeb72713bc08ae1692f53aaf4ebe8a1d662c92bf59e69d6c53dce9"
  "libunwind|c31577d16978b0da0e472ef751f74893a5b459a7ea4a383b75f7ab93cf1e6877"
)
# Tests replace the list (space-separated entries) and the base URL.
if [ -n "${FETCH_LLVM_TARBALLS:-}" ]; then read -r -a TARBALLS <<<"$FETCH_LLVM_TARBALLS"; fi
mkdir -p "$OUT"
for t in "${TARBALLS[@]}"; do
  IFS='|' read -r name sha <<<"$t"
  if [ -d "$OUT/$name" ]; then continue; fi
  TMP="$(mktemp -d "$OUT/.fetch.XXXXXX")"
  curl -fsSL -o "$TMP/a.tar.xz" "$BASE/$name-18.1.8.src.tar.xz" || { rm -rf "$TMP"; echo "fetch_llvm_runtimes: cannot download $name" >&2; exit 1; }
  GOT="$(shasum -a 256 "$TMP/a.tar.xz" | awk '{print $1}')"
  [ "$GOT" = "$sha" ] || { rm -rf "$TMP"; echo "fetch_llvm_runtimes: $name checksum mismatch ($GOT, expected $sha)" >&2; exit 1; }
  tar xf "$TMP/a.tar.xz" -C "$TMP" || { rm -rf "$TMP"; echo "fetch_llvm_runtimes: $name does not unpack" >&2; exit 1; }
  [ -d "$TMP/$name-18.1.8.src" ] || { rm -rf "$TMP"; echo "fetch_llvm_runtimes: $name has no $name-18.1.8.src/" >&2; exit 1; }
  mv "$TMP/$name-18.1.8.src" "$OUT/$name"
  rm -rf "$TMP"
done
echo "fetch_llvm_runtimes: $OUT ready"
