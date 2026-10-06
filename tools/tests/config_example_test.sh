#!/bin/bash
# Following the README (cp config.example.mk config.mk, set GAME) must give clean values.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
# uncomment every variable line, as a user enabling them would
sed -E 's/^# ?([A-Z_]+ *=)/\1/' "$ROOT/config.example.mk" > "$T/config.mk"
printf 'show:\n\t@printf "[%%s][%%s][%%s]" "$(GAME)" "$(APK)" "$(NDK)"\n' > "$T/show.mk"
out="$(cd "$T" && make -s -f config.mk -f show.mk show)"
case "$out" in *" ]"*|*"[ "*|*"#"*) echo "FAIL: values carry spaces or comments: $out"; exit 1 ;; esac
echo "config_example_test: passed"
