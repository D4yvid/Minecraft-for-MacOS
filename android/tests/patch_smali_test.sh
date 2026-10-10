#!/bin/bash
# patch_smali.py must load libmcfm right after gnustl_shared, exactly once.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
# Hand-written stand-in for MainActivity's static initialiser.
cat > "$T/Main.smali" <<'SMALI'
.class public Lcom/mojang/minecraftpe/MainActivity;
.method static constructor <clinit>()V
    .locals 2

    const-string v1, "gnustl_shared"

    invoke-static {v1}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V

    const-string v1, "ovrfmod"

    invoke-static {v1}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V

    return-void
.end method
SMALI
cp "$T/Main.smali" "$T/orig.smali"
python3 -I "$ROOT/android/tools/patch_smali.py" "$T/Main.smali" || fail "patch failed"
loads="$(grep -A1 'const-string v1, "' "$T/Main.smali" | grep -o '"[a-z_]*"' | tr '\n' ' ')"
[ "$loads" = '"gnustl_shared" "mcfm" "ovrfmod" ' ] || fail "load order is $loads"
[ "$(grep -c 'loadLibrary' "$T/Main.smali")" = 3 ] || fail "expected 3 loadLibrary calls"
cp "$T/Main.smali" "$T/once.smali"
python3 -I "$ROOT/android/tools/patch_smali.py" "$T/Main.smali" || fail "second run failed"
cmp -s "$T/Main.smali" "$T/once.smali" || fail "not idempotent"
# An APK that already loads mcfm (loaded first) is left alone.
sed 's/"gnustl_shared"/"mcfm"/' "$T/orig.smali" > "$T/loaded.smali"
cp "$T/loaded.smali" "$T/loaded_before.smali"
python3 -I "$ROOT/android/tools/patch_smali.py" "$T/loaded.smali" || fail "already-patched file rejected"
cmp -s "$T/loaded.smali" "$T/loaded_before.smali" || fail "already-patched file changed"
# No anchor: refuse with a clear error, file untouched.
printf '.class public LFoo;\n' > "$T/foo.smali"
if python3 -I "$ROOT/android/tools/patch_smali.py" "$T/foo.smali" 2>/dev/null; then fail "accepted a file without the gnustl_shared load"; fi
[ "$(cat "$T/foo.smali")" = ".class public LFoo;" ] || fail "modified a file it refused"
[ $fails = 0 ] && echo "patch_smali_test: passed" || { echo "$fails failure(s)"; exit 1; }
