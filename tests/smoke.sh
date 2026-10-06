#!/bin/bash
# usage: smoke.sh <converted.app> [expect-line]
# Launches the app (dylib must already be injected), waits 15 s, asserts the
# expected mcpekbm log line appeared and the process is still alive.
set -uo pipefail
APP="$1"; EXPECT="${2:-mcpekbm: patched}"
LOG="$(mktemp)"
pkill -x minecraftpe2 2>/dev/null; sleep 1
"$APP/minecraftpe2" >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 15); do kill -0 $PID 2>/dev/null || break; sleep 1; done
ALIVE=0; kill -0 $PID 2>/dev/null && ALIVE=1
kill $PID 2>/dev/null
grep 'mcpekbm:' "$LOG"
if [ $ALIVE = 1 ] && grep -q "$EXPECT" "$LOG"; then echo "smoke: passed"; rm -f "$LOG"; exit 0; fi
echo "smoke: FAILED (alive=$ALIVE). Log: $LOG"; tail -20 "$LOG"; exit 1
