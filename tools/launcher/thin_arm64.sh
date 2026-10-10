#!/bin/bash
# usage: thin_arm64.sh <binary> <out>
# The arm64 slice of a fat binary; an arm64-only binary (as many decryptors produce) is copied.
set -euo pipefail
IN="$1"; OUT="$2"
ARCHS="$(lipo -archs "$IN" 2>/dev/null || true)"
case " $ARCHS " in
  *" arm64 "*) ;;
  *) echo "thin_arm64: $IN has no arm64 slice (${ARCHS:-not a Mach-O})" >&2; exit 1 ;;
esac
if [ "$ARCHS" = arm64 ]; then
  cp "$IN" "$OUT"
else
  lipo -thin arm64 "$IN" -output "$OUT"
fi
