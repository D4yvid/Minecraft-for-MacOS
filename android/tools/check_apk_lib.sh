#!/bin/bash
# usage: check_apk_lib.sh <libminecraftpe.so>
# Succeeds only for the Android Minecraft PE 0.15.10 engine: every symbol the Android
# platform looks up must be in the library's dynamic symbol table.
set -uo pipefail
LIB="${1:-}"
die() { echo "check_apk_lib: $*" >&2; exit 1; }
[ -f "$LIB" ] || die "$LIB not found"
for sym in _ZTV21AppPlatform_android23 _ZNK11AppPlatform10getEditionEv \
           _ZNK19AppPlatform_android25getPlatformUIScalingRulesEv _ZN8Keyboard7_inputsE; do
  LC_ALL=C grep -qa "$sym" "$LIB" || die "not Minecraft PE 0.15.10 (no $sym in $LIB)"
done
echo "check_apk_lib: ok ($LIB)"
