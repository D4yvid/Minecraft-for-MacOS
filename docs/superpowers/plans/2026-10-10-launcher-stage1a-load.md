# Launcher Stage 1a — Load the Game Image Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make launcher` turns the user's iOS game binary into `dist/launcher/libminecraftpe.dylib` plus generated stub libraries, and `dist/launcher/mcfm-launch` loads it in a plain macOS process (no Catalyst, no UIKit) with every Apple framework — including libobjc — replaced by logging stubs, running all 3,972 static initializers.

**Architecture:** One Python tool (`tools/launcher/mcfm_image.py`) has three subcommands: `imports` (list a Mach-O's imports with function/data kind), `stubs` (generate C stub sources from that list) and `dylib` (convert the executable into a dylib that macOS dyld accepts). A tiny C runtime (`libmcfm_stubrt.dylib`) logs each stub's first call to stderr and to a census file. `mcfm-launch` checks the LC_UUID of the file, then `dlopen`s it. Stage 1b adds the engine boot on top of `mcfm-launch`.

**Tech Stack:** Python 3 (stdlib only), C11 and C++17 (clang, arm64, macOS 11+), bash, make; `dyld_info`, `vtool`, `codesign`, `lipo` from the Command Line Tools.

**Spec:** `docs/LAUNCHER.md` (Stage 1; patch policy; decisions) and `docs/research/macho-launcher.md` (survey).

## Global Constraints

- Never commit Mojang files: generated stubs, import lists and converted images go to `build/` and `dist/` (git-ignored). `tools/tests/no_game_files_test.sh` must keep passing.
- `make test` passes without game files; every new behaviour has a host test that fails first.
- Host libraries bound to the system: `libSystem`, `libc++`, `libz` only. Every other import (all frameworks, `XSAPITCUI`, **and libobjc**) goes to a generated stub.
- The game's `__objc_*` sections are renamed `__xbjc_*` so the system libobjc never reads the game's ObjC metadata.
- `__PAGEZERO` keeps its load-command slot (fixup opcodes address segments by index): it becomes `__MCFM_PAD`, 0x4000 bytes, no access, directly below `__TEXT`.
- arm64 only; launcher pieces are built with `-arch arm64 -mmacosx-version-min=11.0`.
- No game code runs before the LC_UUID check (`01DFB489-A881-3BDD-8F98-6F016E409625`, `shared/apple/macho_uuid.h`).
- Logs are prefixed `mcfm:`. Python runs as `python3 -I`. Quote every path (paths may contain spaces).
- Do not touch the deprecated Catalyst build (`dist/minecraftpe.app`) or a running game.

## Review Focus

1. Not enough header padding for the rewritten load commands → the converter exits 2 with "header padding" and writes nothing; it never produces a corrupt image (Task 3 test with `-headerpad,0`).
2. The wrong binary (other game version, a random dylib) → `mcfm-launch` refuses before `dlopen`, so no foreign initializer runs (Task 4 test: census stays empty).
3. Running the conversion twice (input is already a dylib) → clear "not an executable" error, exit 2 (Task 3 test).
4. A stub hit from many threads at once (the game starts threads in initializers) → logged exactly once, no crash (Task 2 test with 8 threads).
5. Game files under a path with spaces (`~/My Games/…`) → every script works (Tasks 3 and 5 use a temp dir containing a space).

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/launcher/mcfm_image.py` (create) | `imports`, `stubs`, `dylib` subcommands; the host-library list lives here only |
| `tools/launcher/build_stubs.sh` (create) | build `libmcfm_stubrt.dylib` and one `mcfm_stub_<lib>.dylib` per stubbed library from an imports list |
| `macos/launcher/stub_runtime.h`, `stub_runtime.c` (create) | stub macros and the once-per-symbol logger / census writer |
| `macos/launcher/main.cpp` (create) | `mcfm-launch`: UUID check, `dlopen`, report slide |
| `macos/tools/make_launcher.sh` (create) | game-dependent part of `make launcher`: check, thin, list, stubs, convert, sign |
| `tools/tests/launcher_fixture.sh` (create) | builds the test fixture (fake framework + ObjC executable retagged as iOS) |
| `tools/tests/launcher_imports_test.sh`, `launcher_stubs_test.sh`, `launcher_image_test.sh`, `mcfm_launch_test.sh` (create) | host tests, no game files |
| `Makefile` (modify) | `launcher`, `launcher-check` targets; tests added to `test` |
| `docs/LAUNCHER.md`, `docs/HANDOFF.md`, `README.md`, `CLAUDE.md` (modify) | Stage 1a status, decisions, commands |

Work on a new branch from `main`: `git checkout -b claude/launcher-1a main`.

---

### Task 1: Test fixture and `mcfm_image.py imports`

**Files:**
- Create: `tools/tests/launcher_fixture.sh`
- Create: `tools/launcher/mcfm_image.py`
- Test: `tools/tests/launcher_imports_test.sh`
- Modify: `Makefile` (add the test to `test`)

**Interfaces:**
- Produces: `launcher_fixture.sh <outdir>` → `<outdir>/fakekit.dylib` (install name `/System/Library/Frameworks/FakeKit.framework/FakeKit`, exports `_fakekit_hello`, `_kFakeKitValue`) and `<outdir>/fixture` (thin arm64 `MH_EXECUTE`, platform iOS, classic opcode fixups, imports `FakeKit` and `libobjc`, a static initializer that calls `fakekit_hello()` and sends `+poke` to its own ObjC class).
- Produces: `python3 -I tools/launcher/mcfm_image.py imports <macho>` → stdout TSV lines `<lib>\t<symbol>\t<fn|data>`, one per imported symbol, plus one `<lib>\t-\tlib` line per linked library (so libraries linked without imported symbols, like the game's CoreText, still get a stub); sorted and unique. `<lib>` is the short library name: last path component up to the first `.` (`/usr/lib/libobjc.A.dylib` → `libobjc`, `…/UIKit.framework/UIKit` → `UIKit`).

- [ ] **Step 1: Write the fixture builder**

`tools/tests/launcher_fixture.sh`:
```bash
#!/bin/bash
# usage: launcher_fixture.sh <outdir>
# Builds a stand-in for the game binary: a thin arm64 executable tagged for iOS, with
# classic (opcode) fixups like the 2016 game, importing a fake framework and libobjc,
# with an ObjC class and a static initializer that calls into both.
set -euo pipefail
OUT="$1"; mkdir -p "$OUT"
CC=(clang -arch arm64 -mmacosx-version-min=11.0)
cat > "$OUT/fakekit.c" <<'EOF'
int fakekit_hello(void) { return 1; }
char kFakeKitValue[16] = "real";
EOF
"${CC[@]}" -dynamiclib "$OUT/fakekit.c" \
  -install_name /System/Library/Frameworks/FakeKit.framework/FakeKit -o "$OUT/fakekit.dylib"
cat > "$OUT/fixture.m" <<'EOF'
#import <objc/NSObject.h>
int fakekit_hello(void);
extern char kFakeKitValue[16];
@interface MCFMFixture : NSObject
@end
@implementation MCFMFixture
+ (void)poke {}
@end
__attribute__((constructor)) static void fixture_init(void) {
  fakekit_hello();
  (void)kFakeKitValue[0];
  [MCFMFixture poke];
}
int main(void) { return 0; }
EOF
"${CC[@]}" ${FIXTURE_LDFLAGS:--Wl,-headerpad,0x1000} -fno-objc-arc -fno-objc-msgsend-selector-stubs \
  -Wl,-no_fixup_chains "$OUT/fixture.m" -lobjc "$OUT/fakekit.dylib" -o "$OUT/fixture.mac"
vtool -set-build-version ios 15.0 15.0 -replace -output "$OUT/fixture" "$OUT/fixture.mac"
rm -f "$OUT/fixture.mac"
```

- [ ] **Step 2: Write the failing test**

`tools/tests/launcher_imports_test.sh`:
```bash
#!/bin/bash
# mcfm_image.py imports: lists every import with its library and function/data kind.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" || { echo "FAIL: fixture build"; exit 1; }
OUT="$(python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$T/fixture")" || { echo "FAIL: imports exited $?"; exit 1; }
fails=0
expect() { grep -qxF "$1" <<<"$OUT" || { echo "FAIL: missing line: $1"; fails=$((fails+1)); }; }
expect $'FakeKit\t_fakekit_hello\tfn'
expect $'FakeKit\t_kFakeKitValue\tdata'
expect $'libobjc\t_objc_msgSend\tfn'
expect $'libobjc\t_OBJC_CLASS_$_NSObject\tdata'
expect $'libobjc\t_OBJC_METACLASS_$_NSObject\tdata'
expect $'FakeKit\t-\tlib'
expect $'libobjc\t-\tlib'
expect $'libSystem\t-\tlib'
[ "$(sort -u <<<"$OUT")" = "$OUT" ] || { echo "FAIL: output not sorted/unique"; fails=$((fails+1)); }
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$T/nope" 2>/dev/null && { echo "FAIL: missing file accepted"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_imports_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 3: Run it to verify it fails**

Run: `bash tools/tests/launcher_imports_test.sh`
Expected: FAIL (`mcfm_image.py` does not exist; "imports exited 2").

- [ ] **Step 4: Implement `imports`**

`tools/launcher/mcfm_image.py`:
```python
#!/usr/bin/env python3
"""Mach-O tools for the launcher (docs/LAUNCHER.md, Stage 1).

usage: mcfm_image.py imports <macho>              imports as TSV: lib, symbol, fn|data
       mcfm_image.py stubs <imports.tsv> <outdir> one C stub source per stubbed library
       mcfm_image.py dylib <executable> <out>     executable -> dylib loadable on macOS
Exit 2 when the input is unsuitable.
"""
import os
import re
import subprocess
import sys

# Libraries the image binds to the host system; everything else gets a generated stub.
HOST_LIBS = ("libSystem", "libc++", "libz")

# Non-lazy imports that are data, not functions (constants, ObjC classes, globals).
DATA_NAME = re.compile(
    r"^_(k[A-Z]\w*|OBJC_\w+|_objc_empty_cache|__CFConstantStringClassReference"
    r"|NS\w*(Key|Domain|Notification|Mode)|UI\w*(Notification|Key|Invalid|Image)"
    r"|GC\w*Notification|AVAudioSessionCategory\w*|CGRectZero|_NSConcrete\w+)$")


def fail(msg):
    print("mcfm_image.py: " + msg, file=sys.stderr)
    sys.exit(2)


def short_name(install_path):
    """'/usr/lib/libobjc.A.dylib' -> 'libobjc', '.../UIKit.framework/UIKit' -> 'UIKit'."""
    return install_path.rsplit("/", 1)[-1].split(".")[0]


def list_imports(path):
    """{(lib, symbol): kind} from `dyld_info -fixups`."""
    if not os.path.isfile(path):
        fail("no such file: " + path)
    try:
        text = subprocess.run(["dyld_info", "-fixups", path], check=True,
                              capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        fail("dyld_info failed on %s: %s" % (path, e))
    kinds = {}
    for line in text.splitlines():
        f = line.split()
        if len(f) < 5 or f[3] not in ("bind", "lazy-bind", "weak-bind"):
            continue
        lib, _, sym = f[4].partition("/")
        if not sym:
            continue
        key = (lib, sym)
        lazy = f[3] == "lazy-bind"
        data = not lazy and (f[1] != "__got" or DATA_NAME.match(sym) is not None)
        if lazy or kinds.get(key) == "fn":
            kinds[key] = "fn"
        else:
            kinds[key] = "data" if data else "fn"
    return kinds


def linked_libs(path):
    """Short names of every LC_LOAD_DYLIB / LC_LOAD_WEAK_DYLIB, from `otool -L`."""
    text = subprocess.run(["otool", "-L", path], check=True, capture_output=True, text=True).stdout
    return {short_name(line.split()[0]) for line in text.splitlines()[1:] if line.strip()}


def cmd_imports(path):
    rows = {(lib, sym, kind) for (lib, sym), kind in list_imports(path).items()}
    rows |= {(lib, "-", "lib") for lib in linked_libs(path)}
    for row in sorted(rows):
        print("\t".join(row))


def main(argv):
    if len(argv) == 3 and argv[1] == "imports":
        return cmd_imports(argv[2])
    fail(__doc__.strip())


if __name__ == "__main__":
    main(sys.argv)
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `bash tools/tests/launcher_imports_test.sh`
Expected: `launcher_imports_test: passed`. If `_kFakeKitValue` comes out as `fn`, check the fixture's `dyld_info -fixups` output: the data import must be a non-lazy bind outside `__got`, or in `__got` with a name the regex does not match — adjust only `DATA_NAME`/the rule, not the expectation.

- [ ] **Step 6: Add the test to `make test` and commit**

In `Makefile`, `test:` recipe, after `bash tools/tests/catalyst_deprecated_test.sh` add:
```make
	bash tools/tests/launcher_imports_test.sh
```
Run: `make test` → all pass.
```bash
git add tools/launcher/mcfm_image.py tools/tests/launcher_fixture.sh tools/tests/launcher_imports_test.sh Makefile
git commit -m "Launcher: test fixture and import listing (mcfm_image.py imports)"
```

---

### Task 2: Stub runtime and stub generator

**Files:**
- Create: `macos/launcher/stub_runtime.h`, `macos/launcher/stub_runtime.c`
- Create: `tools/launcher/build_stubs.sh`
- Modify: `tools/launcher/mcfm_image.py` (add `stubs`)
- Test: `tools/tests/launcher_stubs_test.sh`
- Modify: `Makefile` (add the test)

**Interfaces:**
- Consumes: the TSV format from Task 1; `HOST_LIBS`.
- Produces: `mcfm_image.py stubs <tsv> <outdir>` → `<outdir>/<lib>.c` for each non-host library.
- Produces: `build_stubs.sh <tsv> <outdir>` → `<outdir>/libmcfm_stubrt.dylib` (install name `@rpath/libmcfm_stubrt.dylib`) and `<outdir>/mcfm_stub_<lib>.dylib` (install name `@rpath/mcfm_stub_<lib>.dylib`).
- Produces: runtime behaviour: a function stub returns 0 and logs `mcfm: stub <lib>:<symbol>` once; `objc_msgSend`/`objc_msgSendSuper2` stubs return 0 and log `mcfm: stub libobjc:<symbol> <selector>` once per selector (selectors are C strings because the image's selector refs are never fixed up); a data stub is 256 zero bytes. With `MCFM_CENSUS=<file>` each logged line (without the `mcfm: stub ` prefix) is appended to the file.

- [ ] **Step 1: Write the failing test**

`tools/tests/launcher_stubs_test.sh`:
```bash
#!/bin/bash
# Generated stubs: return 0, data is zero, each symbol / selector logged once (also across
# threads), census file written.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T"
printf 'EmptyKit\t-\tlib\nFakeKit\t-\tlib\nFakeKit\t_fakekit_hello\tfn\nFakeKit\t_kFakeKitValue\tdata\nlibobjc\t_objc_msgSend\tfn\nlibSystem\t-\tlib\nlibSystem\t_malloc\tfn\n' > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T/stubs" || { echo "FAIL: build_stubs"; exit 1; }
fails=0
[ -e "$T/stubs/mcfm_stub_libSystem.dylib" ] && { echo "FAIL: host library got a stub"; fails=$((fails+1)); }
[ -e "$T/stubs/mcfm_stub_EmptyKit.dylib" ] || { echo "FAIL: library without symbols got no stub"; fails=$((fails+1)); }
cat > "$T/use.c" <<'EOF'
#include <pthread.h>
#include <stdio.h>
int fakekit_hello(void);
extern char kFakeKitValue[256];
void *objc_msgSend(void *self, const char *sel);
static void *worker(void *arg) { (void)arg; for (int i = 0; i < 1000; i++) fakekit_hello(); return 0; }
int main(void) {
  pthread_t t[8];
  for (int i = 0; i < 8; i++) pthread_create(&t[i], 0, worker, 0);
  for (int i = 0; i < 8; i++) pthread_join(t[i], 0);
  for (int i = 0; i < 256; i++) if (kFakeKitValue[i]) { puts("data not zero"); return 1; }
  if (fakekit_hello() != 0) { puts("fn not 0"); return 1; }
  if (objc_msgSend(0, "poke") || objc_msgSend(0, "poke") || objc_msgSend(0, "other")) { puts("msgSend not 0"); return 1; }
  return 0;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/use.c" "$T/stubs/mcfm_stub_FakeKit.dylib" \
  "$T/stubs/mcfm_stub_libobjc.dylib" -Wl,-rpath,"$T/stubs" -o "$T/use" || { echo "FAIL: link"; exit 1; }
ERR="$(MCFM_CENSUS="$T/census.txt" "$T/use" 2>&1 >/dev/null)" || { echo "FAIL: use exited $?: $ERR"; fails=$((fails+1)); }
count() { grep -cxF "$1" <<<"$ERR"; }
[ "$(count 'mcfm: stub FakeKit:_fakekit_hello')" = 1 ] || { echo "FAIL: fakekit_hello not logged exactly once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend poke')" = 1 ] || { echo "FAIL: poke not logged once"; fails=$((fails+1)); }
[ "$(count 'mcfm: stub libobjc:_objc_msgSend other')" = 1 ] || { echo "FAIL: other not logged once"; fails=$((fails+1)); }
CENSUS="$(cat "$T/census.txt" 2>/dev/null)"
[ "$(wc -l <<<"$CENSUS" | tr -d ' ')" = 3 ] || { echo "FAIL: census should have 3 lines: $CENSUS"; fails=$((fails+1)); }
grep -qxF 'FakeKit:_fakekit_hello' <<<"$CENSUS" || { echo "FAIL: census missing fakekit_hello"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_stubs_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash tools/tests/launcher_stubs_test.sh`
Expected: `FAIL: build_stubs` (script missing).

- [ ] **Step 3: Write the stub runtime**

`macos/launcher/stub_runtime.h`:
```c
#pragma once
// Runtime for the generated framework stubs (tools/launcher/mcfm_image.py stubs).
// A stub returns 0 / nil and logs "mcfm: stub <lib>:<symbol>" the first time it is called;
// with MCFM_CENSUS=<file> the line is also appended to that file.
#ifdef __cplusplus
extern "C" {
#endif
void mcfm_stub_hit(int *seen, const char *lib, const char *symbol);
// objc_msgSend stubs: the selector is a C string (the image's selector refs are never
// registered with a runtime); logged once per selector.
void mcfm_stub_msgsend(const char *lib, const char *symbol, const char *selector);
#ifdef __cplusplus
}
#endif

#define MCFM_STUB_FN(n, lib, sym)                                  \
  void *mcfm_stub_##n(void) __asm__(sym);                          \
  void *mcfm_stub_##n(void) {                                      \
    static int seen;                                               \
    mcfm_stub_hit(&seen, lib, sym);                                \
    return 0;                                                      \
  }

#define MCFM_STUB_MSGSEND(n, lib, sym)                             \
  void *mcfm_stub_##n(void *self, const char *sel) __asm__(sym);  \
  void *mcfm_stub_##n(void *self, const char *sel) {              \
    (void)self;                                                    \
    mcfm_stub_msgsend(lib, sym, sel);                              \
    return 0;                                                      \
  }

#define MCFM_STUB_DATA(n, sym) \
  __attribute__((aligned(16))) char mcfm_data_##n[256] __asm__(sym) = {0};
```

`macos/launcher/stub_runtime.c`:
```c
#include "stub_runtime.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Called with `lock` held.
static void record(const char *lib, const char *symbol, const char *selector) {
  char line[512];
  if (selector)
    snprintf(line, sizeof line, "%s:%s %s", lib, symbol, selector);
  else
    snprintf(line, sizeof line, "%s:%s", lib, symbol);
  fprintf(stderr, "mcfm: stub %s\n", line);
  const char *path = getenv("MCFM_CENSUS");
  if (path && *path) {
    FILE *f = fopen(path, "a");
    if (f) {
      fprintf(f, "%s\n", line);
      fclose(f);
    }
  }
}

void mcfm_stub_hit(int *seen, const char *lib, const char *symbol) {
  pthread_mutex_lock(&lock);
  if (!*seen) {
    *seen = 1;
    record(lib, symbol, 0);
  }
  pthread_mutex_unlock(&lock);
}

// Selectors already logged, keyed by pointer (each selector name is one string in the image).
#define SEL_SLOTS 8192
static const char *logged_sels[SEL_SLOTS];

void mcfm_stub_msgsend(const char *lib, const char *symbol, const char *selector) {
  pthread_mutex_lock(&lock);
  unsigned h = (unsigned)(((uintptr_t)selector >> 3) % SEL_SLOTS);
  for (unsigned i = 0; i < SEL_SLOTS; i++, h = (h + 1) % SEL_SLOTS) {
    if (logged_sels[h] == selector) break;
    if (!logged_sels[h]) {
      logged_sels[h] = selector;
      record(lib, symbol, selector ? selector : "(null)");
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}
```

- [ ] **Step 4: Add `stubs` to `mcfm_image.py`**

Add before `def main`:
```python
MSGSEND = ("_objc_msgSend", "_objc_msgSendSuper2")


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cmd_stubs(tsv, outdir):
    by_lib = {}
    try:
        lines = open(tsv).read().splitlines()
    except OSError as e:
        fail("cannot read %s: %s" % (tsv, e))
    for line in lines:
        if not line.strip():
            continue
        try:
            lib, sym, kind = line.split("\t")
        except ValueError:
            fail("bad line in %s: %r" % (tsv, line))
        if kind not in ("fn", "data", "lib"):
            fail("bad kind in %s: %r" % (tsv, line))
        if lib in HOST_LIBS:
            continue
        syms = by_lib.setdefault(lib, [])  # a "lib" line alone still yields an (empty) stub
        if kind != "lib":
            syms.append((sym, kind))
    os.makedirs(outdir, exist_ok=True)
    for lib, syms in sorted(by_lib.items()):
        out = ["// Generated by tools/launcher/mcfm_image.py stubs; do not edit.",
               '#include "stub_runtime.h"']
        for n, (sym, kind) in enumerate(sorted(syms)):
            if kind == "data":
                out.append("MCFM_STUB_DATA(%d, %s)" % (n, c_string(sym)))
            elif sym in MSGSEND:
                out.append("MCFM_STUB_MSGSEND(%d, %s, %s)" % (n, c_string(lib), c_string(sym)))
            else:
                out.append("MCFM_STUB_FN(%d, %s, %s)" % (n, c_string(lib), c_string(sym)))
        with open(os.path.join(outdir, lib + ".c"), "w") as fh:
            fh.write("\n".join(out) + "\n")
```
and in `main`:
```python
    if len(argv) == 4 and argv[1] == "stubs":
        return cmd_stubs(argv[2], argv[3])
```

- [ ] **Step 5: Write `build_stubs.sh`**

`tools/launcher/build_stubs.sh`:
```bash
#!/bin/bash
# usage: build_stubs.sh <imports.tsv> <outdir>
# Builds libmcfm_stubrt.dylib and mcfm_stub_<lib>.dylib for every stubbed library listed
# in imports.tsv (mcfm_image.py imports). All install names are @rpath/….
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSV="$1"; OUT="$2"
CC=(clang -arch arm64 -mmacosx-version-min=11.0 -O1 -Wall -dynamiclib)
rm -rf "$OUT/src"; mkdir -p "$OUT/src"
"${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -install_name @rpath/libmcfm_stubrt.dylib \
  -o "$OUT/libmcfm_stubrt.dylib"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" stubs "$TSV" "$OUT/src"
for c in "$OUT"/src/*.c; do
  [ -e "$c" ] || continue
  lib="$(basename "$c" .c)"
  "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" "$OUT/libmcfm_stubrt.dylib" \
    -install_name "@rpath/mcfm_stub_$lib.dylib" -o "$OUT/mcfm_stub_$lib.dylib"
done
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `bash tools/tests/launcher_stubs_test.sh`
Expected: `launcher_stubs_test: passed`.

- [ ] **Step 7: Add to `make test` and commit**

`Makefile` `test:` recipe, after the imports test:
```make
	bash tools/tests/launcher_stubs_test.sh
```
Run: `make test` → all pass.
```bash
git add macos/launcher/stub_runtime.h macos/launcher/stub_runtime.c tools/launcher/build_stubs.sh tools/launcher/mcfm_image.py tools/tests/launcher_stubs_test.sh Makefile
git commit -m "Launcher: logging framework stubs (mcfm_image.py stubs, build_stubs.sh)"
```

---

### Task 3: Executable → dylib converter (`mcfm_image.py dylib`)

**Files:**
- Modify: `tools/launcher/mcfm_image.py` (add `dylib`)
- Test: `tools/tests/launcher_image_test.sh`
- Modify: `Makefile` (add the test)

**Interfaces:**
- Consumes: `short_name`, `HOST_LIBS` (Task 1); `build_stubs.sh` (Task 2); `launcher_fixture.sh` (Task 1).
- Produces: `mcfm_image.py dylib <executable> <out>` writes `<out>` (unsigned; caller runs `codesign -f -s -`) with:
  `filetype = MH_DYLIB`, `MH_PIE` cleared, first load command `LC_ID_DYLIB @rpath/libminecraftpe.dylib`;
  `LC_MAIN`, `LC_LOAD_DYLINKER`, `LC_ENCRYPTION_INFO(_64)` removed;
  `LC_VERSION_MIN_*` or `LC_BUILD_VERSION` replaced by `LC_BUILD_VERSION` platform macOS (1), minos/sdk 11.0, no tools;
  `__PAGEZERO` → `__MCFM_PAD` at `__TEXT.vmaddr - 0x4000`, size 0x4000, prot 0;
  every section named `__objc_*` renamed `__xbjc_*`;
  every non-host `LC_LOAD_DYLIB`/`LC_LOAD_WEAK_DYLIB` renamed `@rpath/mcfm_stub_<short>.dylib` (type kept);
  everything else (segments, `LC_DYLD_INFO_ONLY`, symbol tables, `LC_UUID`, `LC_RPATH`, `LC_CODE_SIGNATURE`) unchanged.
  Exit 2 with a message containing `header padding` if the new commands do not fit before the first section's file offset; exit 2 with `not an executable` if `filetype != MH_EXECUTE`; exit 2 with `not a thin arm64` for anything else.

- [ ] **Step 1: Write the failing test**

`tools/tests/launcher_image_test.sh`:
```bash
#!/bin/bash
# mcfm_image.py dylib: the iOS-tagged fixture executable becomes a dylib that macOS dyld
# loads with every non-host library stubbed; libobjc never sees its ObjC metadata; its
# static initializer runs and reaches the stubs.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL=(python3 -I "$ROOT/tools/launcher/mcfm_image.py")
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" || { echo "FAIL: fixture"; exit 1; }
fails=0
"${TOOL[@]}" imports "$T/fixture" > "$T/imports.tsv" || { echo "FAIL: imports"; exit 1; }
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T" || { echo "FAIL: stubs"; exit 1; }
"${TOOL[@]}" dylib "$T/fixture" "$T/libminecraftpe.dylib" || { echo "FAIL: dylib exited $?"; exit 1; }
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null
HDR="$(otool -hv "$T/libminecraftpe.dylib")"; CMDS="$(otool -l "$T/libminecraftpe.dylib")"; LIBS="$(otool -L "$T/libminecraftpe.dylib")"
grep -q " DYLIB " <<<"$HDR" || { echo "FAIL: not MH_DYLIB"; fails=$((fails+1)); }
grep -q " PIE" <<<"$HDR" && { echo "FAIL: PIE flag kept"; fails=$((fails+1)); }
grep -q "@rpath/libminecraftpe.dylib" <<<"$LIBS" || { echo "FAIL: no LC_ID_DYLIB"; fails=$((fails+1)); }
grep -q "@rpath/mcfm_stub_FakeKit.dylib" <<<"$LIBS" || { echo "FAIL: FakeKit not redirected"; fails=$((fails+1)); }
grep -q "@rpath/mcfm_stub_libobjc.dylib" <<<"$LIBS" || { echo "FAIL: libobjc not redirected"; fails=$((fails+1)); }
grep -q "/usr/lib/libSystem.B.dylib" <<<"$LIBS" || { echo "FAIL: libSystem not kept"; fails=$((fails+1)); }
grep -qE "cmd LC_(MAIN|LOAD_DYLINKER)$" <<<"$CMDS" && { echo "FAIL: LC_MAIN/LC_LOAD_DYLINKER kept"; fails=$((fails+1)); }
grep -q "sectname __objc_" <<<"$CMDS" && { echo "FAIL: __objc_ sections visible"; fails=$((fails+1)); }
grep -q "segname __MCFM_PAD" <<<"$CMDS" || { echo "FAIL: no __MCFM_PAD"; fails=$((fails+1)); }
grep -A3 "cmd LC_BUILD_VERSION" <<<"$CMDS" | grep -qE "platform (1|MACOS|macos)$" || { echo "FAIL: not retagged to macOS"; fails=$((fails+1)); }
# Load it: the fixture's initializer must reach the stubs (and not crash in libobjc).
cat > "$T/host.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
  void *h = dlopen(argv[1], RTLD_NOW);
  if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  return 0;
}
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/host.c" -o "$T/host"
OUT="$(MCFM_CENSUS="$T/census.txt" "$T/host" "$T/libminecraftpe.dylib" 2>&1)" || { echo "FAIL: dlopen: $OUT"; fails=$((fails+1)); }
grep -qxF "FakeKit:_fakekit_hello" "$T/census.txt" 2>/dev/null || { echo "FAIL: initializer did not reach FakeKit stub"; fails=$((fails+1)); }
grep -qxF "libobjc:_objc_msgSend poke" "$T/census.txt" 2>/dev/null || { echo "FAIL: initializer did not reach objc_msgSend stub"; fails=$((fails+1)); }
# Already a dylib -> refused, nothing written.
"${TOOL[@]}" dylib "$T/libminecraftpe.dylib" "$T/again.dylib" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "not an executable" "$T/err" && [ ! -e "$T/again.dylib" ]; } || { echo "FAIL: dylib input not refused (rc $rc)"; fails=$((fails+1)); }
# No header padding -> either a loadable result or a clean "header padding" refusal.
mkdir -p "$T/tight"
FIXTURE_LDFLAGS="-Wl,-headerpad,0" bash "$ROOT/tools/tests/launcher_fixture.sh" "$T/tight" >/dev/null 2>&1
"${TOOL[@]}" dylib "$T/tight/fixture" "$T/tight/out.dylib" 2>"$T/err"; rc=$?
if [ $rc = 2 ]; then
  { grep -q "header padding" "$T/err" && [ ! -e "$T/tight/out.dylib" ]; } || { echo "FAIL: tight header: bad refusal"; fails=$((fails+1)); }
elif [ $rc = 0 ]; then
  otool -l "$T/tight/out.dylib" >/dev/null 2>&1 || { echo "FAIL: tight header: corrupt output"; fails=$((fails+1)); }
else
  echo "FAIL: tight header: exit $rc"; fails=$((fails+1))
fi
[ $fails = 0 ] && echo "launcher_image_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash tools/tests/launcher_image_test.sh`
Expected: `FAIL: dylib exited 2` (usage message: `dylib` not implemented).

- [ ] **Step 3: Implement `dylib`**

Add to `mcfm_image.py` (add `import struct` at the top):
```python
MH_MAGIC_64, CPU_TYPE_ARM64 = 0xFEEDFACF, 0x0100000C
MH_EXECUTE, MH_DYLIB, MH_PIE = 2, 6, 0x200000
LC_SEGMENT_64, LC_LOAD_DYLIB, LC_ID_DYLIB, LC_LOAD_DYLINKER = 0x19, 0xC, 0xD, 0xE
LC_LOAD_WEAK_DYLIB, LC_MAIN, LC_BUILD_VERSION = 0x80000018, 0x80000028, 0x32
LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64 = 0x21, 0x2C
LC_VERSION_MIN = (0x24, 0x25, 0x2F, 0x30)  # macOS, iOS, tvOS, watchOS
PLATFORM_MACOS, MACOS_11 = 1, 0x000B0000
PAD_SIZE = 0x4000
IMAGE_ID = "@rpath/libminecraftpe.dylib"


def dylib_command(cmd, name):
    raw = name.encode() + b"\0"
    size = (24 + len(raw) + 7) & ~7
    return struct.pack("<IIIIII", cmd, size, 24, 2, 0x10000, 0x10000) + raw.ljust(size - 24, b"\0")


def segment_name(cmd_bytes):
    return cmd_bytes[8:24].rstrip(b"\0").decode()


def cmd_dylib(src, dst):
    try:
        data = bytearray(open(src, "rb").read())
    except OSError as e:
        fail("cannot read %s: %s" % (src, e))
    if len(data) < 32:
        fail("not a thin arm64 Mach-O: " + src)
    magic, cpu, sub, ftype, ncmds, sizeofcmds, flags, res = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64 or cpu != CPU_TYPE_ARM64:
        fail("not a thin arm64 Mach-O (lipo -thin arm64 first): " + src)
    if ftype != MH_EXECUTE:
        fail("not an executable (already converted?): " + src)

    cmds, off = [], 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        cmds.append(bytes(data[off:off + size]))
        off += size
    text_vmaddr = next(struct.unpack_from("<Q", c, 24)[0] for c in cmds
                       if struct.unpack_from("<I", c)[0] == LC_SEGMENT_64 and segment_name(c) == "__TEXT")

    first_data, out = len(data), [dylib_command(LC_ID_DYLIB, IMAGE_ID)]
    for c in cmds:
        cmd = struct.unpack_from("<I", c)[0]
        if cmd in (LC_MAIN, LC_LOAD_DYLINKER, LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64):
            continue
        if cmd in LC_VERSION_MIN or cmd == LC_BUILD_VERSION:
            out.append(struct.pack("<IIIIII", LC_BUILD_VERSION, 24, PLATFORM_MACOS, MACOS_11, MACOS_11, 0))
            continue
        if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB):
            name_off = struct.unpack_from("<I", c, 8)[0]
            name = c[name_off:].split(b"\0")[0].decode()
            lib = short_name(name)
            out.append(c if lib in HOST_LIBS else dylib_command(cmd, "@rpath/mcfm_stub_%s.dylib" % lib))
            continue
        if cmd == LC_SEGMENT_64:
            c = bytearray(c)
            if segment_name(c) == "__PAGEZERO":
                # Keep the slot: fixup opcodes address segments by index.
                c[8:24] = b"__MCFM_PAD".ljust(16, b"\0")
                struct.pack_into("<QQ", c, 24, text_vmaddr - PAD_SIZE, PAD_SIZE)
            nsects = struct.unpack_from("<I", c, 64)[0]
            for k in range(nsects):
                s = 72 + 80 * k
                if c[s:s + 7] == b"__objc_":  # hide ObjC metadata from the system libobjc
                    c[s + 2:s + 3] = b"x"
                sect_off, sect_size = struct.unpack_from("<I", c, s + 48)[0], struct.unpack_from("<Q", c, s + 40)[0]
                if sect_off and sect_size:
                    first_data = min(first_data, sect_off)
            c = bytes(c)
        out.append(c)

    blob = b"".join(out)
    if 32 + len(blob) > first_data:
        fail("not enough header padding (%d bytes of load commands, %d available): %s"
             % (len(blob), first_data - 32, src))
    data[32:32 + max(sizeofcmds, len(blob))] = b"\0" * max(sizeofcmds, len(blob))
    data[32:32 + len(blob)] = blob
    struct.pack_into("<IiiIIIII", data, 0, magic, cpu, sub, MH_DYLIB, len(out), len(blob), flags & ~MH_PIE, res)
    with open(dst, "wb") as fh:
        fh.write(data)
```
Note: `first_data` must be computed from the **original** section offsets of all segments before writing; the loop above does that while building `out`, and the size check happens before anything is written, so a refusal never creates `dst`.

In `main`:
```python
    if len(argv) == 4 and argv[1] == "dylib":
        return cmd_dylib(argv[2], argv[3])
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `bash tools/tests/launcher_image_test.sh`
Expected: `launcher_image_test: passed`. If `dlopen` fails, print `"$OUT"`: dyld names the problem (missing symbol → the imports list or stub kinds; "rebase … non-writable segment" → a segment slot was removed; crash in `readClass` → an `__objc_` section was missed).

- [ ] **Step 5: Add to `make test` and commit**

`Makefile` `test:` recipe, after the stubs test:
```make
	bash tools/tests/launcher_image_test.sh
```
Run: `make test` → all pass.
```bash
git add tools/launcher/mcfm_image.py tools/tests/launcher_image_test.sh Makefile
git commit -m "Launcher: convert the game executable into a macOS-loadable dylib"
```

---

### Task 4: `mcfm-launch`

**Files:**
- Create: `macos/launcher/main.cpp`
- Test: `tools/tests/mcfm_launch_test.sh`
- Modify: `Makefile` (`$(BUILD)/launcher/mcfm-launch` rule, test)

**Interfaces:**
- Consumes: `mcfm::is_expected_game_image(const void *header)` (`shared/apple/macho_uuid.h`, reads only header + load commands; works on a file buffer and on a loaded image).
- Produces: `mcfm-launch [image]` (default `<dir of mcfm-launch>/libminecraftpe.dylib`). Exit 2 + `mcfm: cannot load <path>: …` if the file is unreadable or `dlopen` fails; exit 3 + `mcfm: <path> is not Minecraft PE 0.15.10 (LC_UUID)` **before** `dlopen` if the UUID does not match; on success prints `mcfm: game image loaded at 0x<header> (slide 0x<slide>)` and exits 0. Stage 1b replaces the exit with the engine boot.
- Produces: Make variable `LAUNCHER_BIN := $(BUILD)/launcher/mcfm-launch`.

- [ ] **Step 1: Write the failing test**

`tools/tests/mcfm_launch_test.sh`:
```bash
#!/bin/bash
# mcfm-launch: refuses a non-game image before loading it (no foreign initializer runs)
# and reports unreadable paths. The real game is covered by make launcher-check.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
make -s build/launcher/mcfm-launch >/dev/null || { echo "FAIL: build mcfm-launch"; exit 1; }
BIN="$ROOT/build/launcher/mcfm-launch"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash tools/tests/launcher_fixture.sh "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
python3 -I tools/launcher/mcfm_image.py imports "$T/fixture" > "$T/imports.tsv"
bash tools/launcher/build_stubs.sh "$T/imports.tsv" "$T" >/dev/null
python3 -I tools/launcher/mcfm_image.py dylib "$T/fixture" "$T/libminecraftpe.dylib"
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null
fails=0
OUT="$(MCFM_CENSUS="$T/census.txt" "$BIN" "$T/libminecraftpe.dylib" 2>&1)"; rc=$?
[ $rc = 3 ] || { echo "FAIL: fixture image: exit $rc, want 3: $OUT"; fails=$((fails+1)); }
grep -q "is not Minecraft PE 0.15.10" <<<"$OUT" || { echo "FAIL: no UUID message: $OUT"; fails=$((fails+1)); }
[ -e "$T/census.txt" ] && { echo "FAIL: fixture initializers ran before the UUID check"; fails=$((fails+1)); }
OUT="$("$BIN" "$T/missing.dylib" 2>&1)"; rc=$?
{ [ $rc = 2 ] && grep -q "cannot load" <<<"$OUT"; } || { echo "FAIL: missing file: exit $rc: $OUT"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "mcfm_launch_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash tools/tests/mcfm_launch_test.sh`
Expected: `FAIL: build mcfm-launch` (no Makefile rule).

- [ ] **Step 3: Write `mcfm-launch`**

`macos/launcher/main.cpp`:
```cpp
// mcfm-launch: loads the converted game image (make launcher) in a plain macOS process.
// usage: mcfm-launch [image]   (default: libminecraftpe.dylib next to this executable)
// The LC_UUID is checked on the file before dlopen, so no other image's code runs.
#include "macho_uuid.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string executable_dir() {
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return ".";
  std::string path(buf);
  return path.substr(0, path.rfind('/'));
}

// Header + load commands of the file (the UUID lives there).
bool read_header(const std::string &path, std::vector<char> *out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(64 * 1024, 0);
  f.read(out->data(), out->size());
  return f.gcount() >= 32;
}

}  // namespace

int main(int argc, char **argv) {
  std::string path = argc > 1 ? argv[1] : executable_dir() + "/libminecraftpe.dylib";
  std::vector<char> header;
  if (!read_header(path, &header)) {
    std::fprintf(stderr, "mcfm: cannot load %s: unreadable\n", path.c_str());
    return 2;
  }
  if (!mcfm::is_expected_game_image(header.data())) {
    std::fprintf(stderr, "mcfm: %s is not Minecraft PE 0.15.10 (LC_UUID)\n", path.c_str());
    return 3;
  }
  if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
    std::fprintf(stderr, "mcfm: cannot load %s: %s\n", path.c_str(), dlerror());
    return 2;
  }
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const mach_header *h = _dyld_get_image_header(i);
    if (mcfm::is_expected_game_image(h)) {
      std::printf("mcfm: game image loaded at %p (slide 0x%lx)\n", static_cast<const void *>(h),
                  static_cast<unsigned long>(_dyld_get_image_vmaddr_slide(i)));
      return 0;
    }
  }
  std::fprintf(stderr, "mcfm: %s loaded but not found among dyld images\n", path.c_str());
  return 2;
}
```

- [ ] **Step 4: Add the Makefile rule**

In `Makefile`, after the iOS section, add a section:
```make
# ---------------------------------------------------------------- Mach-O launcher (docs/LAUNCHER.md)
LAUNCHER_BIN := $(BUILD)/launcher/mcfm-launch
LAUNCHER_CXXFLAGS := -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -O2 -Wall -Wextra -Ishared/apple

$(LAUNCHER_BIN): macos/launcher/main.cpp shared/apple/macho_uuid.cpp shared/apple/macho_uuid.h
	@mkdir -p $(dir $@)
	clang++ $(LAUNCHER_CXXFLAGS) macos/launcher/main.cpp shared/apple/macho_uuid.cpp \
	  -Wl,-rpath,@executable_path -o $@
```
and in the `test:` recipe, after the image test:
```make
	bash tools/tests/mcfm_launch_test.sh
```

- [ ] **Step 5: Run the tests**

Run: `bash tools/tests/mcfm_launch_test.sh` → `mcfm_launch_test: passed`; then `make test` → all pass.

- [ ] **Step 6: Commit**

```bash
git add macos/launcher/main.cpp tools/tests/mcfm_launch_test.sh Makefile
git commit -m "Launcher: mcfm-launch loads the converted image after an LC_UUID check"
```

---

### Task 5: `make launcher` / `make launcher-check` with the real game, docs

**Files:**
- Create: `macos/tools/make_launcher.sh`
- Modify: `Makefile` (`launcher`, `launcher-check`, `.PHONY`)
- Modify: `docs/LAUNCHER.md`, `docs/HANDOFF.md`, `README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `tools/check_game.sh <app>` (UUID + decrypted check), `mcfm_image.py imports|dylib`, `build_stubs.sh`, `$(LAUNCHER_BIN)`.
- Produces: `make launcher GAME=<minecraftpe2.app>` → `dist/launcher/` containing `mcfm-launch`, `libminecraftpe.dylib`, `libmcfm_stubrt.dylib`, `mcfm_stub_*.dylib` (all ad hoc signed) and `imports.tsv`. `make launcher-check` runs `dist/launcher/mcfm-launch` with `MCFM_CENSUS=build/launcher/census.txt` and fails unless it prints `game image loaded` and the census contains `libobjc:_objc_autoreleasePoolPush` (proof the initializers ran into the stubs).

- [ ] **Step 1: Write the failing check (Makefile target first)**

Add to the launcher section of the `Makefile`:
```make
LAUNCHER_OUT ?= $(CURDIR)/dist/launcher

.PHONY: launcher launcher-check
launcher: $(LAUNCHER_BIN)
	@test -n "$(GAME)" || { echo "Set GAME=<your decrypted minecraftpe2.app> (or put it in config.mk)"; exit 1; }
	bash macos/tools/make_launcher.sh "$(GAME)" "$(LAUNCHER_OUT)" "$(LAUNCHER_BIN)"

# Loads the image built by make launcher; the census lists every stub the game called.
launcher-check:
	@rm -f $(BUILD)/launcher/census.txt
	@OUT="$$(MCFM_CENSUS="$(CURDIR)/$(BUILD)/launcher/census.txt" "$(LAUNCHER_OUT)/mcfm-launch" 2>&1)"; \
	  echo "$$OUT" | grep "^mcfm: game image" ; \
	  echo "$$OUT" | grep -q "game image loaded" || { echo "$$OUT" | tail -20; echo "launcher-check: FAILED"; exit 1; }
	@grep -qxF "libobjc:_objc_autoreleasePoolPush" $(BUILD)/launcher/census.txt || { echo "launcher-check: initializers did not reach the stubs"; exit 1; }
	@echo "launcher-check: passed ($$(wc -l < $(BUILD)/launcher/census.txt | tr -d ' ') stubs called, see $(BUILD)/launcher/census.txt)"
```
Run: `make launcher GAME="$PWD/../../../game-files/ios/Payload/minecraftpe2.app"` (from the worktree; from the main checkout `GAME` defaults to `game-files/ios/Payload/minecraftpe2.app`).
Expected: FAIL — `macos/tools/make_launcher.sh: No such file or directory`.

- [ ] **Step 2: Write `make_launcher.sh`**

`macos/tools/make_launcher.sh`:
```bash
#!/bin/bash
# usage: make_launcher.sh <minecraftpe2.app> <outdir> <mcfm-launch>
# Builds the Mach-O launcher directory from your decrypted game: the arm64 executable
# converted to libminecraftpe.dylib, stubs for every non-host library it imports, and
# mcfm-launch. Nothing here is committed (dist/ is git-ignored).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$1"; OUT="$2"; BIN="$3"
bash "$ROOT/tools/check_game.sh" "$APP"
EXE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist")"
rm -rf "$OUT"; mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
lipo -thin arm64 "$APP/$EXE" -output "$TMP/game"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$TMP/game" > "$OUT/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$OUT/imports.tsv" "$OUT"
rm -rf "$OUT/src"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$TMP/game" "$OUT/libminecraftpe.dylib"
cp "$BIN" "$OUT/mcfm-launch"
for f in "$OUT"/*.dylib "$OUT/mcfm-launch"; do codesign -f -s - "$f" 2>/dev/null; done
echo "make_launcher: $OUT ready ($(ls "$OUT"/mcfm_stub_*.dylib | wc -l | tr -d ' ') stub libraries)"
```

- [ ] **Step 3: Build and check with the real game**

Run: `make launcher GAME=<path to game-files/ios/Payload/minecraftpe2.app>` → `make_launcher: … ready (18 stub libraries)` (17 frameworks incl. CoreText with an empty stub, plus libobjc).
Run: `make launcher-check` → `mcfm: game image loaded at 0x… (slide 0x…)` and `launcher-check: passed (N stubs called, …)`.
If `dlopen` fails, `mcfm-launch` prints dyld's reason: a missing symbol means a wrong `fn`/`data` kind or a missing library line; fix it in `mcfm_image.py` with a test in Task 1/2 style first.

- [ ] **Step 4: Docs**

- `docs/LAUNCHER.md`: in Stage 1 replace the task list header with "Stage 1 is split: **1a** load the image (☑ when this plan lands, plan `docs/superpowers/plans/2026-10-10-launcher-stage1a-load.md`), **1b** AppPlatform + boot + ANGLE window + input, **1c** seams, audio, census-driven fixes." Mark item 1 (image preparation) and 2 (stub libraries) ☑ and item 9 (runtime census) ◐ (initializer census exists). Under Decisions add: "2026-10-10: libobjc is stubbed too (the system libobjc must not read the image's ObjC metadata; `__objc_*` sections are renamed `__xbjc_*`); `__PAGEZERO` becomes a 16 KB `__MCFM_PAD` segment (fixups address segments by index)." and fix the earlier decision line to "host: libSystem, libc++, libz only".
- `docs/HANDOFF.md` §3 "Daily commands": add `make launcher` / `make launcher-check` lines; §6.0: "Stage 1a (image loads with all frameworks stubbed) done; 1b next."
- `README.md`: after the Catalyst quick start add a "Launcher (in progress)" paragraph: `make launcher` then `make launcher-check` — loads the game without Catalyst; nothing playable yet.
- `CLAUDE.md` Commands: add "- `make launcher` / `launcher-check` — Mach-O launcher (docs/LAUNCHER.md): converted image + stubs in dist/launcher; census in build/launcher/census.txt."

- [ ] **Step 5: Full verification and commit**

Run: `make test` → all pass (no game files needed). Run `make launcher-check` once more.
```bash
git add Makefile macos/tools/make_launcher.sh docs/LAUNCHER.md docs/HANDOFF.md README.md CLAUDE.md
git commit -m "Launcher: make launcher / launcher-check build and load the real game image"
```
