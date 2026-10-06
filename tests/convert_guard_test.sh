#!/bin/bash
# convert.sh must refuse output paths that could delete anything but its own output.
# Works only inside a temp dir; pkill is stubbed so a running game is never touched.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/work/orig/minecraftpe2.app" "$T/plain" "$T/other.app"
printf '#!/bin/sh\nexit 0\n' > "$T/bin/pkill"; chmod +x "$T/bin/pkill"
SRC="$T/work/orig/minecraftpe2.app"
printf '#!/bin/sh\n' > "$SRC/minecraftpe2"; chmod +x "$SRC/minecraftpe2"
touch "$T/plain/keep" "$T/other.app/keep"
fails=0
refuses() {  # refuses <desc> <dst> <file that must survive>
  if PATH="$T/bin:$PATH" bash "$ROOT/tools/convert.sh" "$SRC" "$2" >/dev/null 2>&1; then
    echo "FAIL: $1 was accepted"; fails=$((fails+1))
  fi
  [ -e "$3" ] || { echo "FAIL: $1 deleted $3"; fails=$((fails+1)); }
  [ -x "$SRC/minecraftpe2" ] || { echo "FAIL: $1 deleted the original"; fails=$((fails+1)); }
}
refuses "ancestor of the original" "$T/work" "$SRC/minecraftpe2"
refuses "plain folder" "$T/plain" "$T/plain/keep"
refuses "foreign .app without marker" "$T/other.app" "$T/other.app/keep"
refuses "the original itself" "$SRC" "$SRC/minecraftpe2"
refuses "inside the original" "$SRC/sub.app" "$SRC/minecraftpe2"
[ ! -e "$SRC/sub.app" ] || { echo "FAIL: created a folder inside the original"; fails=$((fails+1)); }
# a running game must not be killed (unsaved progress): refuse and keep the old build
mkdir -p "$T/out/minecraftpe2.app"; touch "$T/out/minecraftpe2.app/.mcpekbm-generated" "$T/out/minecraftpe2.app/keep"
printf '#!/bin/sh\nexit 0\n' > "$T/bin/pgrep"; chmod +x "$T/bin/pgrep"   # "game is running"
refuses "rebuild while the game runs" "$T/out/minecraftpe2.app" "$T/out/minecraftpe2.app/keep"
[ $fails = 0 ] && echo "convert_guard_test: passed" || { echo "$fails failure(s)"; exit 1; }
