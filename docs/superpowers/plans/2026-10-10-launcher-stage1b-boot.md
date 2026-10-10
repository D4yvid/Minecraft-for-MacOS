# Launcher Stage 1b — Boot the Engine in a Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make launcher-run` opens a macOS window in which the converted game image boots — our own `AppPlatform`, `MinecraftClient` constructed and initialised, frames rendered through ANGLE (OpenGL ES 3 on Metal) — and `make launcher-check` proves it by rendering 120 frames without crashing.

**Architecture:** Platform-free C++11 in `shared/launcher/` builds our AppPlatform vtable (base AppPlatform vtable + our slot implementations + desktop/Win10 policy) and runs the engine boot sequence; seam replacements (starting with the Xbox Live config singleton) reach the engine through a **hook table**: `mcfm_image.py dylib --hooks` patches each hooked function's first 12 bytes into `adrp/ldr/br` through a table placed in the free tail of `__DATA`, and `mcfm-launch` fills the table from a dyld add-image callback, before any game initializer runs. The `OpenGLES` stub re-exports ANGLE's `libGLESv2` (prebuilt, from an official Electron release, fetched with a pinned SHA-256). `mcfm-launch` becomes an AppKit app with a `CAMetalLayer` window, EGL context and frame loop; EGL/GL entry points are loaded with `dlopen` so building and `make test` never need ANGLE.

**Tech Stack:** C++11 (shared), Objective-C++17 (AppKit host), Python 3, bash, make; ANGLE `libEGL.dylib`/`libGLESv2.dylib` (Metal backend) from Electron's macOS arm64 release.

**Spec:** `docs/LAUNCHER.md` (Stage 1 items 3–5, Patch policy, Decisions) and `docs/research/macho-launcher.md` (boot sequence, seams, key addresses). Spike results that shaped this plan are recorded in Task 7's doc step.

## Global Constraints

- Everything from Stage 1a still holds: no Mojang files committed; `make test` passes without game files **and without ANGLE**; host libraries `libSystem`, `libc++`, `libz` only; no game code before the LC_UUID check; logs `mcfm:`; `python3 -I`; quoted paths; never touch the Catalyst build or a running game.
- `shared/launcher/` is C++11 and platform-free (no AppKit, no dyld, no EGL headers); host code lives in `macos/launcher/`.
- Code in the game image is never patched at runtime (Apple Silicon kills processes that execute modified signed pages): text patches are applied by `mcfm_image.py dylib --hooks` before signing; runtime writes go only to the hook table in `__DATA`.
- A hooked function must be at least 12 bytes long and must not be called by a static initializer before the add-image callback fills the table (the callback runs before initializers, so this holds for `mcfm-launch`).
- ANGLE comes only from the pinned official Electron release archive, verified by SHA-256; it lives in `build/angle/` (git-ignored) and is copied into `dist/launcher/`. Downloading it is an explicit, separate step (`make angle`).
- Launcher storage is separate from the Catalyst build's worlds: `~/Library/Application Support/MinecraftPE-mcfm/` (`external/`, `internal/`, `userdata/`, `tmp/`). Sharing worlds with the Catalyst build is a later owner decision.
- Slot numbers are AppPlatform vtable indices from `docs/research/appplatform-vtable.md`; App vtable: 19 `update()`, 20 `setUISizeAndScale(int, int, float)`, 21 `setRenderingSize(int, int)`.

## Review Focus

1. A hook address that is not inside `__TEXT`, or more hooks than the table holds → the converter refuses with exit 2 and writes nothing (Task 1 test).
2. The game image loaded without `--hooks` (old `dist/launcher`) while `mcfm-launch` fills hooks → the filler must find the table by the same rule and never write outside `__DATA` (Task 1 C++ test: table address inside the segment; Task 6 fills only when the hooks TSV matches).
3. ANGLE archive with a wrong checksum, or missing the two libraries → `fetch_angle.sh` refuses and leaves no partial `build/angle` (Task 5 test).
4. A slot that returns `std::string` by value must be called with the engine's ABI (x8 result pointer, `this` in x0) → Task 3 tests call every implemented slot through a function pointer of the engine's signature.
5. Window resize, including to a tiny size → `resize()` forwards the new framebuffer size in pixels (not points) and never 0×0 (Task 6: clamped to ≥ 1, unit-tested helper).

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/launcher/mcfm_image.py` (modify) | `dylib --hooks <tsv>`: patch hooked entries; `stubs --provided <lib> <symbols>`: leave provider symbols out |
| `tools/launcher/build_stubs.sh` (modify) | `--provider <lib>=<dylib>`: stub re-exports the provider |
| `tools/launcher/fetch_angle.sh` (create) | download pinned Electron release, verify SHA-256, extract ANGLE dylibs |
| `shared/apple/hook_table.h/.cpp` (create) | locate the hook table in a loaded/converted image (same rule as the converter) |
| `shared/apple/addresses_0_15_10.h` (modify) | boot and seam addresses |
| `shared/launcher/app_platform.h/.cpp` (create) | `HostInfo`, `build_vtable`, all slot implementations, desktop policy |
| `shared/launcher/engine.h/.cpp` (create) | `EngineAddresses`, `Engine::start/frame/resize` |
| `shared/launcher/seams.h/.cpp` (create) | hook list (name, unslid address, replacement); Xbox config replacement |
| `shared/tests/launcher_app_platform_test.cpp`, `launcher_engine_test.cpp` (create) | host tests |
| `macos/launcher/main.mm` (replaces `main.cpp`) | CLI, UUID check, hook filling, window, EGL, frame loop |
| `macos/launcher/egl_min.h` (create) | the few EGL/GL declarations used, loaded with `dlsym` |
| `macos/launcher/resize_math.h` (create) | pixel-size helper (unit-tested) |
| `macos/tools/make_launcher.sh` (modify) | hooks + ANGLE provider + copy ANGLE |
| `tools/tests/launcher_hooks_test.sh`, `launcher_provider_test.sh`, `fetch_angle_test.sh` (create) | host tests |
| `Makefile` (modify) | new tests, `angle`, `launcher-run`, `launcher-check --frames` |
| `docs/LAUNCHER.md`, `docs/research/macho-launcher.md`, `docs/HANDOFF.md`, `README.md`, `CLAUDE.md` (modify) | status, spike findings |

Branch: `claude/launcher-1b` (already created from `main`).

---

### Task 1: Hook table (converter `--hooks` + C++ locator)

**Files:**
- Modify: `tools/launcher/mcfm_image.py`, `tools/tests/launcher_fixture.sh`
- Create: `shared/apple/hook_table.h`, `shared/apple/hook_table.cpp`
- Test: `tools/tests/launcher_hooks_test.sh`
- Modify: `Makefile` (test)

**Interfaces:**
- Produces: `mcfm_image.py dylib <exe> <out> [--hooks <hooks.tsv>]`; `hooks.tsv` lines `<name>\t0x<unslid address>`; hook *i* jumps through `table + 8*i`. Table rule: `table = align16(max(addr + size) over all sections of segment __DATA)`; capacity `(seg.vmaddr + seg.vmsize - table) / 8`. Exit 2 with `hook table full` or `not in __TEXT`.
- Produces: `uintptr_t mcfm::hook_table_address(const void *header)` — unslid table address by the same rule from header + load commands; 0 without `__DATA`. Plus `size_t mcfm::hook_table_capacity(const void *header)`.
- Produces: fixture function `fixture_answer(int)` (≥ 12 bytes, called by the fixture's static initializer).

- [ ] **Step 1: Extend the fixture**

In `tools/tests/launcher_fixture.sh`, in `fixture.m`, add before `__attribute__((constructor))`:
```objc
// Long enough (>= 12 bytes at -O0) to carry a hook; called by the initializer below.
__attribute__((noinline)) int fixture_answer(int x) { return x * 3 + 41; }
```
and inside `fixture_init`, after `[MCFMFixture poke];`:
```objc
  volatile int answer = fixture_answer(0);
  (void)answer;
```
Run: `bash tools/tests/launcher_image_test.sh && bash tools/tests/launcher_imports_test.sh` → both still pass.

- [ ] **Step 2: Write the failing test**

`tools/tests/launcher_hooks_test.sh`:
```bash
#!/bin/bash
# dylib --hooks: a hooked function jumps through the hook table; a host that fills the table
# from a dyld add-image callback (before initializers) runs its replacement instead.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL=(python3 -I "$ROOT/tools/launcher/mcfm_image.py")
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
bash "$ROOT/tools/tests/launcher_fixture.sh" "$T" >/dev/null || { echo "FAIL: fixture"; exit 1; }
fails=0
ADDR="$(nm "$T/fixture" | awk '$3=="_fixture_answer"{print "0x"$1}')"
[ -n "$ADDR" ] || { echo "FAIL: no _fixture_answer symbol"; exit 1; }
printf 'fixture_answer\t%s\n' "$ADDR" > "$T/hooks.tsv"
"${TOOL[@]}" imports "$T/fixture" > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T" >/dev/null
"${TOOL[@]}" dylib "$T/fixture" "$T/libminecraftpe.dylib" --hooks "$T/hooks.tsv" || { echo "FAIL: dylib --hooks exited $?"; exit 1; }
codesign -f -s - "$T/libminecraftpe.dylib" 2>/dev/null
cat > "$T/host.cpp" <<'EOF'
#include "hook_table.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstring>
static int replaced = 0;
static int replacement(int x) { replaced++; return x + 1000; }
static void on_add(const mach_header *h, intptr_t slide) {
  Dl_info info;
  if (!dladdr(h, &info) || !strstr(info.dli_fname, "libminecraftpe")) return;
  uintptr_t table = mcfm::hook_table_address(h);
  if (!table || mcfm::hook_table_capacity(h) < 1) return;
  reinterpret_cast<void **>(table + slide)[0] = reinterpret_cast<void *>(&replacement);
}
int main(int, char **argv) {
  _dyld_register_func_for_add_image(on_add);
  if (!dlopen(argv[1], RTLD_NOW)) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
  std::printf("replaced=%d\n", replaced);
  return replaced == 1 ? 0 : 2;
}
EOF
clang++ -arch arm64 -mmacosx-version-min=11.0 -std=c++11 -I "$ROOT/shared/apple" "$T/host.cpp" \
  "$ROOT/shared/apple/hook_table.cpp" -Wl,-rpath,@executable_path -o "$T/host" || { echo "FAIL: host build"; exit 1; }
OUT="$("$T/host" "$T/libminecraftpe.dylib" 2>&1)" || { echo "FAIL: hook not taken: $OUT"; fails=$((fails+1)); }
# Refusals: address outside __TEXT, too many hooks; nothing written.
printf 'bad\t0x10\n' > "$T/bad.tsv"
"${TOOL[@]}" dylib "$T/fixture" "$T/bad.dylib" --hooks "$T/bad.tsv" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "not in __TEXT" "$T/err" && [ ! -e "$T/bad.dylib" ]; } || { echo "FAIL: bad address not refused (rc $rc)"; fails=$((fails+1)); }
python3 -c "import sys; [print('h%d\t%s' % (i, sys.argv[1])) for i in range(100000)]" "$ADDR" > "$T/many.tsv"
"${TOOL[@]}" dylib "$T/fixture" "$T/many.dylib" --hooks "$T/many.tsv" 2>"$T/err"; rc=$?
{ [ $rc = 2 ] && grep -q "hook table full" "$T/err" && [ ! -e "$T/many.dylib" ]; } || { echo "FAIL: overfull table not refused (rc $rc)"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_hooks_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 3: Run it to verify it fails**

Run: `bash tools/tests/launcher_hooks_test.sh`
Expected: FAIL — the usage message (`dylib` takes no `--hooks` yet) → `FAIL: dylib --hooks exited 2`.

- [ ] **Step 4: Implement the C++ locator**

`shared/apple/hook_table.h`:
```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace mcfm {

// The launcher's hook table in a converted game image (tools/launcher/mcfm_image.py dylib
// --hooks): the first 16-byte aligned address after the last section of __DATA, up to the end
// of the segment. Hook i jumps through table[i]. Both read only the header + load commands.
uintptr_t hook_table_address(const void *header);   // unslid; 0 if there is no __DATA
size_t hook_table_capacity(const void *header);     // entries; 0 if there is no __DATA

}  // namespace mcfm
```
`shared/apple/hook_table.cpp`:
```cpp
#include "hook_table.h"

#include <cstring>

namespace mcfm {
namespace {

constexpr uint32_t kMagic64 = 0xFEEDFACF, kLcSegment64 = 0x19, kHeaderSize = 32;

template <class T> T rd(const uint8_t *p) { T v; std::memcpy(&v, p, sizeof v); return v; }

// Unslid [table, segment end) of __DATA; false if absent or malformed.
bool data_tail(const void *header, uint64_t *table, uint64_t *end) {
  const uint8_t *h = static_cast<const uint8_t *>(header);
  if (!h || rd<uint32_t>(h) != kMagic64) return false;
  uint32_t ncmds = rd<uint32_t>(h + 16), sizeofcmds = rd<uint32_t>(h + 20);
  uint32_t off = kHeaderSize, limit = kHeaderSize + sizeofcmds;
  for (uint32_t i = 0; i < ncmds && off + 8 <= limit; i++) {
    uint32_t cmd = rd<uint32_t>(h + off), size = rd<uint32_t>(h + off + 4);
    if (size < 8 || off + size > limit) return false;
    if (cmd == kLcSegment64 && size >= 72 && std::strncmp(reinterpret_cast<const char *>(h + off + 8), "__DATA", 16) == 0) {
      uint64_t vmaddr = rd<uint64_t>(h + off + 24), vmsize = rd<uint64_t>(h + off + 32);
      uint32_t nsects = rd<uint32_t>(h + off + 64);
      if (72 + 80ull * nsects > size) return false;
      uint64_t last = vmaddr;
      for (uint32_t k = 0; k < nsects; k++) {
        const uint8_t *s = h + off + 72 + 80 * k;
        uint64_t sect_end = rd<uint64_t>(s + 32) + rd<uint64_t>(s + 40);
        if (sect_end > last) last = sect_end;
      }
      *table = (last + 15) & ~uint64_t(15);
      *end = vmaddr + vmsize;
      return *table <= *end;
    }
    off += size;
  }
  return false;
}

}  // namespace

uintptr_t hook_table_address(const void *header) {
  uint64_t table, end;
  return data_tail(header, &table, &end) ? static_cast<uintptr_t>(table) : 0;
}

size_t hook_table_capacity(const void *header) {
  uint64_t table, end;
  return data_tail(header, &table, &end) ? static_cast<size_t>((end - table) / 8) : 0;
}

}  // namespace mcfm
```

- [ ] **Step 5: Implement `--hooks` in the converter**

In `mcfm_image.py`, add after `IMAGE_ID = ...`:
```python
def read_hooks(path):
    """[(name, unslid address)] from '<name>\\t0x<address>' lines."""
    hooks = []
    try:
        lines = open(path).read().splitlines()
    except OSError as e:
        fail("cannot read %s: %s" % (path, e))
    for line in lines:
        if not line.strip():
            continue
        try:
            name, addr = line.split("\t")
            hooks.append((name, int(addr, 16)))
        except ValueError:
            fail("bad line in %s: %r" % (path, line))
    return hooks


def segment_info(cmds, name):
    """(vmaddr, vmsize, fileoff, [(addr, size)] sections) of an LC_SEGMENT_64, or None."""
    for c in cmds:
        if struct.unpack_from("<I", c)[0] == LC_SEGMENT_64 and segment_name(c) == name:
            vmaddr, vmsize, fileoff = struct.unpack_from("<QQQ", c, 24)
            nsects = struct.unpack_from("<I", c, 64)[0]
            sects = [struct.unpack_from("<QQ", c, 72 + 80 * k + 32) for k in range(nsects)]
            return vmaddr, vmsize, fileoff, sects
    return None


def patch_hooks(data, cmds, hooks):
    """Replace each hooked function's first 12 bytes with adrp x16 / ldr x16 / br x16 through
    the hook table (end of __DATA; same rule as shared/apple/hook_table.cpp)."""
    text, data_seg = segment_info(cmds, "__TEXT"), segment_info(cmds, "__DATA")
    if not text or not data_seg:
        fail("no __TEXT or __DATA segment")
    dvm, dsize, _, dsects = data_seg
    table = (max([dvm] + [a + s for a, s in dsects]) + 15) & ~15
    if len(hooks) > (dvm + dsize - table) // 8:
        fail("hook table full: %d hooks, room for %d" % (len(hooks), (dvm + dsize - table) // 8))
    tvm, tsize, tfileoff, _ = text
    for i, (name, addr) in enumerate(hooks):
        if not (tvm <= addr and addr + 12 <= tvm + tsize):
            fail("hook %s at 0x%x is not in __TEXT" % (name, addr))
        slot = table + 8 * i
        pages = (slot >> 12) - (addr >> 12)
        adrp = 0x90000000 | ((pages & 3) << 29) | (((pages >> 2) & 0x7FFFF) << 5) | 16
        ldr = 0xF9400000 | (((slot & 0xFFF) // 8) << 10) | (16 << 5) | 16
        br = 0xD61F0200
        struct.pack_into("<III", data, addr - tvm + tfileoff, adrp, ldr, br)
```
In `cmd_dylib`, change the signature to `def cmd_dylib(src, dst, hooks=())`, and immediately before `data[32:32 + max(sizeofcmds, len(blob))] = ...` add:
```python
    if hooks:
        patch_hooks(data, cmds, hooks)
```
In `main`, replace the `dylib` dispatch with:
```python
    if len(argv) in (4, 6) and argv[1] == "dylib":
        hooks = ()
        if len(argv) == 6:
            if argv[4] != "--hooks":
                fail(__doc__.strip())
            hooks = read_hooks(argv[5])
        return cmd_dylib(argv[2], argv[3], hooks)
```
and the docstring line to `mcfm_image.py dylib <executable> <out> [--hooks <hooks.tsv>]`.

- [ ] **Step 6: Run the tests**

Run: `bash tools/tests/launcher_hooks_test.sh` → `launcher_hooks_test: passed`; `bash tools/tests/launcher_image_test.sh` → passed.

- [ ] **Step 7: Add to `make test` and commit**

`Makefile` `test:` recipe, after `bash tools/tests/mcfm_launch_test.sh`:
```make
	bash tools/tests/launcher_hooks_test.sh
```
Run: `make test` → all pass.
```bash
git add tools/launcher/mcfm_image.py tools/tests/launcher_fixture.sh tools/tests/launcher_hooks_test.sh shared/apple/hook_table.h shared/apple/hook_table.cpp Makefile
git commit -m "Launcher: hook table — converter patches hooked entries, host fills the table"
```

---

### Task 2: Stub providers (OpenGLES → ANGLE)

**Files:**
- Modify: `tools/launcher/mcfm_image.py` (`stubs --provided`), `tools/launcher/build_stubs.sh` (`--provider`)
- Test: `tools/tests/launcher_provider_test.sh`
- Modify: `Makefile` (test)

**Interfaces:**
- Produces: `mcfm_image.py stubs <tsv> <outdir> [--provided <lib> <symbols-file>]...` — symbols listed in `<symbols-file>` (one per line, with leading `_`) are not stubbed for `<lib>`.
- Produces: `build_stubs.sh <tsv> <outdir> [--provider <lib>=<dylib>]...` — `mcfm_stub_<lib>.dylib` re-exports `<dylib>` (`-Wl,-reexport_library`) and stubs only what it does not export (`nm -gUj`). `<dylib>` must be found at runtime through `@rpath` (copied next to the stubs).

- [ ] **Step 1: Write the failing test**

`tools/tests/launcher_provider_test.sh`:
```bash
#!/bin/bash
# A provider library replaces stubs: its exports win (re-exported), the rest stays stubbed.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
mkdir -p "$T/stubs"
printf 'int fakekit_hello(void) { return 7; }\n' > "$T/prov.c"
clang -arch arm64 -mmacosx-version-min=11.0 -dynamiclib "$T/prov.c" -install_name @rpath/libprov.dylib -o "$T/stubs/libprov.dylib"
printf 'FakeKit\t-\tlib\nFakeKit\t_fakekit_hello\tfn\nFakeKit\t_fakekit_other\tfn\n' > "$T/imports.tsv"
bash "$ROOT/tools/launcher/build_stubs.sh" "$T/imports.tsv" "$T/stubs" --provider "FakeKit=$T/stubs/libprov.dylib" \
  || { echo "FAIL: build_stubs --provider"; exit 1; }
fails=0
grep -q "_fakekit_hello" "$T/stubs/src/FakeKit.c" && { echo "FAIL: provided symbol still stubbed"; fails=$((fails+1)); }
grep -q "_fakekit_other" "$T/stubs/src/FakeKit.c" || { echo "FAIL: missing stub for unprovided symbol"; fails=$((fails+1)); }
cat > "$T/use.c" <<'EOF'
int fakekit_hello(void);
int fakekit_other(void);
int main(void) { return fakekit_hello() == 7 && fakekit_other() == 0 ? 0 : 1; }
EOF
clang -arch arm64 -mmacosx-version-min=11.0 "$T/use.c" "$T/stubs/mcfm_stub_FakeKit.dylib" -Wl,-rpath,"$T/stubs" -o "$T/use" \
  || { echo "FAIL: link against stub"; exit 1; }
"$T/use" 2>/dev/null || { echo "FAIL: provider not re-exported or stub not 0"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "launcher_provider_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash tools/tests/launcher_provider_test.sh`
Expected: `FAIL: build_stubs --provider` (unknown argument → `set -u`/usage failure).

- [ ] **Step 3: Implement**

In `mcfm_image.py`, change `def cmd_stubs(tsv, outdir):` to `def cmd_stubs(tsv, outdir, provided=None):` and right after `if lib in HOST_LIBS: continue` insert:
```python
        if provided and sym in provided.get(lib, ()):
            by_lib.setdefault(lib, [])
            continue
```
In `main`, replace the `stubs` dispatch with:
```python
    if len(argv) >= 4 and argv[1] == "stubs" and (len(argv) - 4) % 3 == 0:
        provided = {}
        for k in range(4, len(argv), 3):
            if argv[k] != "--provided":
                fail(__doc__.strip())
            try:
                provided[argv[k + 1]] = set(open(argv[k + 2]).read().split())
            except OSError as e:
                fail("cannot read %s: %s" % (argv[k + 2], e))
        return cmd_stubs(argv[2], argv[3], provided)
```
and the docstring line to `mcfm_image.py stubs <imports.tsv> <outdir> [--provided <lib> <symbols>]...`.

Replace `tools/launcher/build_stubs.sh` with:
```bash
#!/bin/bash
# usage: build_stubs.sh <imports.tsv> <outdir> [--provider <lib>=<dylib>]...
# Builds libmcfm_stubrt.dylib and mcfm_stub_<lib>.dylib for every stubbed library listed
# in imports.tsv (mcfm_image.py imports). A provider's exports are not stubbed: the stub
# re-exports the provider (e.g. OpenGLES=libGLESv2.dylib from ANGLE). Install names @rpath/….
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSV="$1"; OUT="$2"; shift 2
CC=(clang -arch arm64 -mmacosx-version-min=11.0 -O1 -Wall -dynamiclib)
rm -rf "$OUT/src"; mkdir -p "$OUT/src"
PROVIDED=(); declare -a PROVIDER_LIBS=(); declare -a PROVIDER_DYLIBS=()
while [ $# -gt 0 ]; do
  [ "$1" = "--provider" ] && [ $# -ge 2 ] || { echo "build_stubs.sh: bad argument $1" >&2; exit 2; }
  lib="${2%%=*}"; dylib="${2#*=}"
  nm -gUj "$dylib" > "$OUT/src/$lib.provided"
  PROVIDED+=(--provided "$lib" "$OUT/src/$lib.provided")
  PROVIDER_LIBS+=("$lib"); PROVIDER_DYLIBS+=("$dylib")
  shift 2
done
"${CC[@]}" "$ROOT/macos/launcher/stub_runtime.c" -install_name @rpath/libmcfm_stubrt.dylib \
  -o "$OUT/libmcfm_stubrt.dylib"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" stubs "$TSV" "$OUT/src" ${PROVIDED[@]+"${PROVIDED[@]}"}
for c in "$OUT"/src/*.c; do
  [ -e "$c" ] || continue
  lib="$(basename "$c" .c)"
  extra=()
  for i in ${PROVIDER_LIBS[@]+"${!PROVIDER_LIBS[@]}"}; do
    [ "${PROVIDER_LIBS[$i]}" = "$lib" ] && extra=(-Wl,-reexport_library,"${PROVIDER_DYLIBS[$i]}")
  done
  "${CC[@]}" -I "$ROOT/macos/launcher" -Wno-unused-parameter "$c" "$OUT/libmcfm_stubrt.dylib" \
    ${extra[@]+"${extra[@]}"} -install_name "@rpath/mcfm_stub_$lib.dylib" -o "$OUT/mcfm_stub_$lib.dylib"
done
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/tests/launcher_provider_test.sh` → passed; `bash tools/tests/launcher_stubs_test.sh` → passed.

- [ ] **Step 5: Add to `make test` and commit**

`Makefile` `test:` recipe, after the hooks test: `	bash tools/tests/launcher_provider_test.sh`. Run `make test` → all pass.
```bash
git add tools/launcher/mcfm_image.py tools/launcher/build_stubs.sh tools/tests/launcher_provider_test.sh Makefile
git commit -m "Launcher: stub providers — a real library replaces stubs for what it exports"
```

---

### Task 3: Our AppPlatform (shared vtable and slots)

**Files:**
- Create: `shared/launcher/app_platform.h`, `shared/launcher/app_platform.cpp`
- Test: `shared/tests/launcher_app_platform_test.cpp`
- Modify: `Makefile` (test target)

**Interfaces:**
- Produces (`namespace mcfm::launcher`):
  - `struct HostInfo { std::string data_dir, external_dir, internal_dir, userdata_dir, temp_dir, region, device_id; }` — `data_dir` ends with `/`; `region` like `en_US`.
  - `constexpr size_t kBaseSlots = 101;`
  - `struct EngineFns { void *graphics_vendor, *graphics_renderer, *graphics_version, *graphics_extensions; };`
  - `void set_host_info(const HostInfo &)` (copied; read by the slots).
  - `void build_vtable(void **out, void *const *base, const EngineFns &fns)` — `out[i] = base[i]` except the slots below.
- Slot table (index → behaviour; signatures as the engine calls them, `self` first):

| Slot | Name | Implementation |
|---|---|---|
| 2, 4 | getDataUrl, getPackagePath | `std::string` = `data_dir` |
| 18 | swapBuffers | no-op (the host presents) |
| 20 | getSystemRegion | `const std::string &` = `region` |
| 21–24 | getGraphicsVendor/Renderer/Version/Extensions | engine functions from `EngineFns` |
| 25 | pickImage(ImagePickingCallback&) | no-op |
| 33, 34, 35, 100 | getExternalStoragePath, getInternalStoragePath, getUserdataPath, getPlatformTempPath | `const std::string &` of the matching dir |
| 53 | getAssetFileFullPath(const std::string&) | `std::string` = `data_dir + rel` |
| 66, 68 | useMetadataDrivenScreens, useCenteredGUI | `true` |
| 69 | getPlatformType | `0` |
| 74 | getApplicationId | `"com.mojang.minecraftpe"` |
| 80 | getDeviceId | `device_id` |
| 81 | createUUID | random RFC 4122 v4, lowercase |
| 82, 83, 84 | isFirstSnoopLaunch, hasHardwareInformationChanged, isTablet | `false` |
| 93 | getEdition | `"win10"` |
| 96 | getDefaultInputMode | `1` (mouse) |
| 99 | getPlatformUIScalingRules | `0` |

- [ ] **Step 1: Write the failing test**

`shared/tests/launcher_app_platform_test.cpp`:
```cpp
// Our AppPlatform vtable: base slots kept, ours called with the engine's ABI.
#include "app_platform.h"

#include <cstdio>
#include <string>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string fake_vendor(void *) { return "vendor"; }
static int base_fn_marker[kBaseSlots];

template <class R, class... A> R call(void **vt, int slot, A... a) {
  return reinterpret_cast<R (*)(void *, A...)>(vt[slot])(nullptr, a...);
}

int main() {
  void *base[kBaseSlots], *vt[kBaseSlots];
  for (size_t i = 0; i < kBaseSlots; i++) base[i] = &base_fn_marker[i];
  EngineFns fns = {reinterpret_cast<void *>(&fake_vendor), &base_fn_marker[1], &base_fn_marker[2], &base_fn_marker[3]};
  HostInfo info;
  info.data_dir = "/game/data/"; info.external_dir = "/s/ext"; info.internal_dir = "/s/int";
  info.userdata_dir = "/s/user"; info.temp_dir = "/s/tmp"; info.region = "en_US"; info.device_id = "dev-1";
  set_host_info(info);
  build_vtable(vt, base, fns);

  const int ours[] = {2, 4, 18, 20, 21, 22, 23, 24, 25, 33, 34, 35, 53, 66, 68, 69, 74, 80, 81, 82, 83, 84, 93, 96, 99, 100};
  for (size_t i = 0; i < kBaseSlots; i++) {
    bool mine = false;
    for (int s : ours) mine |= (s == static_cast<int>(i));
    if (!mine) EXPECT(vt[i] == base[i]);
  }
  EXPECT(call<std::string>(vt, 2) == "/game/data/");
  EXPECT(call<std::string>(vt, 4) == "/game/data/");
  EXPECT(call<std::string>(vt, 21) == "vendor");
  EXPECT(vt[22] == &base_fn_marker[1]);
  EXPECT((call<const std::string &>(vt, 20)) == "en_US");
  EXPECT((call<const std::string &>(vt, 33)) == "/s/ext");
  EXPECT((call<const std::string &>(vt, 34)) == "/s/int");
  EXPECT((call<const std::string &>(vt, 35)) == "/s/user");
  EXPECT((call<const std::string &>(vt, 100)) == "/s/tmp");
  const std::string rel = "lang/en_US.lang";
  EXPECT((call<std::string, const std::string &>(vt, 53, rel)) == "/game/data/lang/en_US.lang");
  EXPECT(call<bool>(vt, 66) && call<bool>(vt, 68));
  EXPECT(call<int>(vt, 69) == 0);
  EXPECT(call<std::string>(vt, 74) == "com.mojang.minecraftpe");
  EXPECT(call<std::string>(vt, 80) == "dev-1");
  std::string u = call<std::string>(vt, 81), v = call<std::string>(vt, 81);
  EXPECT(u.size() == 36 && u[8] == '-' && u[13] == '-' && u[18] == '-' && u[23] == '-' && u[14] == '4');
  EXPECT(u.find_first_not_of("0123456789abcdef-") == std::string::npos);
  EXPECT(u != v);
  EXPECT(!call<bool>(vt, 82) && !call<bool>(vt, 83) && !call<bool>(vt, 84));
  EXPECT(call<std::string>(vt, 93) == "win10");
  EXPECT(call<int>(vt, 96) == 1);
  EXPECT(call<int>(vt, 99) == 0);
  call<void>(vt, 18);
  call<void, void *>(vt, 25, nullptr);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_app_platform_test: all passed\n");
  return 0;
}
```
`Makefile` (host tests section):
```make
LAUNCHER_SHARED_INC := -Ishared/launcher -Ishared/apple
$(BUILD)/test/launcher_app_platform_test: shared/tests/launcher_app_platform_test.cpp shared/launcher/app_platform.cpp shared/launcher/app_platform.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_app_platform_test.cpp shared/launcher/app_platform.cpp -o $@
```
and add `launcher_app_platform_test` to `SHARED_TESTS`.

- [ ] **Step 2: Run it to verify it fails**

Run: `make build/test/launcher_app_platform_test`
Expected: FAIL — `app_platform.h` not found / no rule for `shared/launcher/app_platform.cpp`.

- [ ] **Step 3: Implement**

`shared/launcher/app_platform.h`:
```cpp
#pragma once
// Our AppPlatform for the Mach-O launcher (docs/LAUNCHER.md): the engine's base AppPlatform
// object with a vtable we build — base slots kept, pure-virtual ones implemented, desktop
// (Win10 UI) policy set. Slot numbers: docs/research/appplatform-vtable.md. C++11.
#include <cstddef>
#include <string>

namespace mcfm {
namespace launcher {

struct HostInfo {
  std::string data_dir;      // the game's data/ directory, ending with '/'
  std::string external_dir;  // worlds live in <external_dir>/games/com.mojang
  std::string internal_dir;
  std::string userdata_dir;
  std::string temp_dir;
  std::string region;        // language_REGION, e.g. en_US (as iOS reports it)
  std::string device_id;
};

constexpr size_t kBaseSlots = 101;  // base AppPlatform vtable (iOS adds 101-102)

// Engine functions some slots point at directly (slid addresses).
struct EngineFns {
  void *graphics_vendor, *graphics_renderer, *graphics_version, *graphics_extensions;
};

void set_host_info(const HostInfo &info);

// out[i] = base[i] for every slot we do not implement.
void build_vtable(void **out, void *const *base, const EngineFns &fns);

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/app_platform.cpp`:
```cpp
#include "app_platform.h"

#include <cstdio>
#include <random>

namespace mcfm {
namespace launcher {
namespace {

// Slot functions: `self` is the engine's `this`; std::string results use the engine's ABI
// (returned through x8), which a free function returning std::string matches.
HostInfo &host() { static HostInfo info; return info; }

std::string data_url(void *) { return host().data_dir; }
void no_op(void *) {}
void pick_image(void *, void *) {}
const std::string &region(void *) { return host().region; }
const std::string &external_dir(void *) { return host().external_dir; }
const std::string &internal_dir(void *) { return host().internal_dir; }
const std::string &userdata_dir(void *) { return host().userdata_dir; }
const std::string &temp_dir(void *) { return host().temp_dir; }
std::string asset_full_path(void *, const std::string &rel) { return host().data_dir + rel; }
bool yes(void *) { return true; }
bool no(void *) { return false; }
int zero(void *) { return 0; }
int mouse_input(void *) { return 1; }
std::string application_id(void *) { return "com.mojang.minecraftpe"; }
std::string device_id(void *) { return host().device_id; }
std::string edition(void *) { return "win10"; }

std::string create_uuid(void *) {
  static std::mt19937_64 rng{std::random_device{}()};
  unsigned char b[16];
  for (int i = 0; i < 16; i += 8) {
    unsigned long long r = rng();
    for (int k = 0; k < 8; k++) b[i + k] = static_cast<unsigned char>(r >> (8 * k));
  }
  b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);  // version 4
  b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);  // RFC 4122 variant
  char s[37];
  std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return s;
}

template <class F> void *fn(F f) { return reinterpret_cast<void *>(f); }

}  // namespace

void set_host_info(const HostInfo &info) { host() = info; }

void build_vtable(void **out, void *const *base, const EngineFns &fns) {
  for (size_t i = 0; i < kBaseSlots; i++) out[i] = base[i];
  out[2] = fn(&data_url);           // getDataUrl
  out[4] = fn(&data_url);           // getPackagePath
  out[18] = fn(&no_op);             // swapBuffers: the host presents
  out[20] = fn(&region);            // getSystemRegion
  out[21] = fns.graphics_vendor;
  out[22] = fns.graphics_renderer;
  out[23] = fns.graphics_version;
  out[24] = fns.graphics_extensions;
  out[25] = fn(&pick_image);        // pickImage
  out[33] = fn(&external_dir);      // getExternalStoragePath
  out[34] = fn(&internal_dir);      // getInternalStoragePath
  out[35] = fn(&userdata_dir);      // getUserdataPath
  out[53] = fn(&asset_full_path);   // getAssetFileFullPath
  out[66] = fn(&yes);               // useMetadataDrivenScreens
  out[68] = fn(&yes);               // useCenteredGUI
  out[69] = fn(&zero);              // getPlatformType: desktop
  out[74] = fn(&application_id);    // getApplicationId
  out[80] = fn(&device_id);         // getDeviceId
  out[81] = fn(&create_uuid);       // createUUID
  out[82] = fn(&no);                // isFirstSnoopLaunch
  out[83] = fn(&no);                // hasHardwareInformationChanged
  out[84] = fn(&no);                // isTablet
  out[93] = fn(&edition);           // getEdition
  out[96] = fn(&mouse_input);       // getDefaultInputMode
  out[99] = fn(&zero);              // getPlatformUIScalingRules: desktop
  out[100] = fn(&temp_dir);         // getPlatformTempPath
}

}  // namespace launcher
}  // namespace mcfm
```

- [ ] **Step 4: Run the test**

Run: `make build/test/launcher_app_platform_test && build/test/launcher_app_platform_test`
Expected: `launcher_app_platform_test: all passed`.

- [ ] **Step 5: Commit**

Run: `make test` → all pass.
```bash
git add shared/launcher/app_platform.h shared/launcher/app_platform.cpp shared/tests/launcher_app_platform_test.cpp Makefile
git commit -m "Launcher: our AppPlatform vtable (pure slots, paths, desktop policy)"
```

---

### Task 4: Engine boot sequence and the Xbox config seam

**Files:**
- Modify: `shared/apple/addresses_0_15_10.h`
- Create: `shared/launcher/engine.h`, `shared/launcher/engine.cpp`, `shared/launcher/seams.h`, `shared/launcher/seams.cpp`
- Test: `shared/tests/launcher_engine_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `HostInfo`, `EngineFns`, `build_vtable`, `set_host_info`, `kBaseSlots` (Task 3).
- Produces (`namespace mcfm::launcher`):
  - `struct EngineAddresses { uintptr_t platform_ctor, base_vtable, client_ctor, app_init, graphics_vendor, graphics_renderer, graphics_version, graphics_extensions; static EngineAddresses for_slide(uintptr_t slide); };`
  - `class Engine { public: bool start(const EngineAddresses &, const HostInfo &, int width, int height); void frame(); void resize(int width, int height); void *app() const; void *platform() const; };` — `start` allocates the platform (0x210 bytes, zeroed), calls the base ctor, installs our vtable, creates `AppContext` (16 zeroed bytes), allocates the client (0x428 zeroed), calls its ctor `(app, 0, nullptr)`, `App::init(app, ctx)`, then `setRenderingSize(w, h)` (slot 21) and `setUISizeAndScale(w, h, 0.f)` (slot 20); `frame()` calls slot 19; `resize()` calls 21 then 20.
  - `struct Hook { const char *name; uintptr_t address; void *replacement; };` `const Hook *hooks(size_t *count);` — the order defines hook-table indices.
- Produces: address constants (below).

- [ ] **Step 1: Add the addresses**

Append to `shared/apple/addresses_0_15_10.h` before the closing `}  // namespace addr`:
```cpp
// Mach-O launcher boot (docs/research/macho-launcher.md, "Boot sequence")
constexpr uintptr_t kFnAppPlatformCtor = 0x10045F678;      // AppPlatform::AppPlatform(), sets the singleton
constexpr uintptr_t kBaseAppPlatformVtable = 0x100E649C0;  // vptr value of the base AppPlatform
constexpr uintptr_t kAppPlatformSingleton = 0x100F5E850;
constexpr uintptr_t kAppPlatformSize = 0x210;              // AppPlatform_iOS; the base object is 360 bytes
constexpr uintptr_t kFnMinecraftClientCtor = 0x10006E2DC;  // (this, int argc, char **argv)
constexpr uintptr_t kMinecraftClientSize = 0x428;
constexpr uintptr_t kFnAppInit = 0x1000555BC;              // App::init(AppContext &)
constexpr uintptr_t kFnGraphicsVendor = 0x10003A850;       // std::string from glGetString(GL_VENDOR)
constexpr uintptr_t kFnGraphicsRenderer = 0x10003A8AC;
constexpr uintptr_t kFnGraphicsVersion = 0x10003A680;
constexpr uintptr_t kFnGraphicsExtensions = 0x10003A908;
constexpr int kAppSlotUpdate = 19;                         // App::update()
// Seams (hooked by the launcher)
constexpr uintptr_t kFnXblAppConfig = 0x100798B34;         // Xbox services config singleton (seam #3)
```

- [ ] **Step 2: Write the failing test**

`shared/tests/launcher_engine_test.cpp`:
```cpp
// Engine boot order with fake engine functions; the Xbox config seam replacement.
#include "engine.h"
#include "seams.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<std::string> calls;
static void *base_vtable[kBaseSlots];
static void *seen_platform_vptr = nullptr, *init_ctx = nullptr;
static int last_w, last_h;

static void platform_ctor(void *self) { calls.push_back("platform_ctor"); *static_cast<void **>(self) = base_vtable; }
static void app_update(void *) { calls.push_back("update"); }
static void app_set_size_and_scale(void *, int w, int h, float s) { calls.push_back("ui_size"); last_w = w; last_h = h; EXPECT(s == 0.f); }
static void app_set_size(void *, int w, int h) { calls.push_back("size"); last_w = w; last_h = h; }
static void *app_vtable[22];
static void client_ctor(void *self, int argc, char **argv) {
  calls.push_back("client_ctor");
  EXPECT(argc == 0 && argv == nullptr);
  seen_platform_vptr = nullptr;
  *static_cast<void **>(self) = app_vtable;
}
static void app_init(void *, void *ctx) { calls.push_back("init"); init_ctx = ctx; }
static std::string gfx(void *) { return "gfx"; }

int main() {
  for (auto &p : base_vtable) p = reinterpret_cast<void *>(&app_update);
  app_vtable[19] = reinterpret_cast<void *>(&app_update);
  app_vtable[20] = reinterpret_cast<void *>(&app_set_size_and_scale);
  app_vtable[21] = reinterpret_cast<void *>(&app_set_size);
  EngineAddresses a;
  a.platform_ctor = reinterpret_cast<uintptr_t>(&platform_ctor);
  a.base_vtable = reinterpret_cast<uintptr_t>(base_vtable);
  a.client_ctor = reinterpret_cast<uintptr_t>(&client_ctor);
  a.app_init = reinterpret_cast<uintptr_t>(&app_init);
  a.graphics_vendor = a.graphics_renderer = a.graphics_version = a.graphics_extensions = reinterpret_cast<uintptr_t>(&gfx);
  HostInfo info;
  info.data_dir = "/d/";
  Engine engine;
  EXPECT(engine.start(a, info, 1280, 720));
  const char *want[] = {"platform_ctor", "client_ctor", "init", "size", "ui_size"};
  EXPECT(calls.size() == 5);
  for (size_t i = 0; i < 5 && i < calls.size(); i++) EXPECT(calls[i] == want[i]);
  EXPECT(last_w == 1280 && last_h == 720);
  EXPECT(init_ctx != nullptr);
  void **vt = *static_cast<void ***>(engine.platform());
  EXPECT(vt != base_vtable);  // ours, not the base one
  EXPECT(reinterpret_cast<std::string (*)(void *)>(vt[2])(nullptr) == "/d/");
  calls.clear();
  engine.frame();
  engine.resize(800, 600);
  EXPECT(calls.size() == 3 && calls[0] == "update" && calls[1] == "size" && calls[2] == "ui_size");
  EXPECT(last_w == 800 && last_h == 600);

  EngineAddresses s = EngineAddresses::for_slide(0x1000);
  EXPECT(s.platform_ctor == 0x10045F678 + 0x1000 && s.app_init == 0x1000555BC + 0x1000);

  size_t n = 0;
  const Hook *h = hooks(&n);
  EXPECT(n >= 1 && std::strcmp(h[0].name, "xbl_app_config") == 0 && h[0].address == 0x100798B34);
  struct SharedPtrOut { void *ptr; void *ctrl; ~SharedPtrOut() {} };  // engine's std::shared_ptr ABI
  SharedPtrOut c1 = reinterpret_cast<SharedPtrOut (*)()>(h[0].replacement)();
  SharedPtrOut c2 = reinterpret_cast<SharedPtrOut (*)()>(h[0].replacement)();
  EXPECT(c1.ptr != nullptr && c1.ctrl == nullptr && c1.ptr == c2.ptr);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_engine_test: all passed\n");
  return 0;
}
```
`Makefile`:
```make
$(BUILD)/test/launcher_engine_test: shared/tests/launcher_engine_test.cpp shared/launcher/engine.cpp shared/launcher/engine.h shared/launcher/seams.cpp shared/launcher/seams.h shared/launcher/app_platform.cpp shared/apple/addresses_0_15_10.h
	@mkdir -p $(dir $@)
	clang++ -std=c++11 -Wall -Wextra -O1 $(LAUNCHER_SHARED_INC) shared/tests/launcher_engine_test.cpp shared/launcher/engine.cpp shared/launcher/seams.cpp shared/launcher/app_platform.cpp -o $@
```
Add `launcher_engine_test` to `SHARED_TESTS`.

- [ ] **Step 3: Run it to verify it fails**

Run: `make build/test/launcher_engine_test`
Expected: FAIL — `engine.h` not found.

- [ ] **Step 4: Implement**

`shared/launcher/engine.h`:
```cpp
#pragma once
// Boots the game engine the way minecraftpeViewController does on iOS (awakeFromNib,
// initView, drawFrame), with our AppPlatform instead of AppPlatform_iOS. C++11.
#include <cstdint>

#include "app_platform.h"

namespace mcfm {
namespace launcher {

// Slid addresses of the engine functions the boot sequence calls.
struct EngineAddresses {
  uintptr_t platform_ctor, base_vtable, client_ctor, app_init;
  uintptr_t graphics_vendor, graphics_renderer, graphics_version, graphics_extensions;
  static EngineAddresses for_slide(uintptr_t slide);  // addresses_0_15_10.h + slide
};

class Engine {
 public:
  // Needs a current GL context. Sizes are framebuffer pixels.
  bool start(const EngineAddresses &addresses, const HostInfo &info, int width, int height);
  void frame();                         // App::update()
  void resize(int width, int height);   // setRenderingSize + setUISizeAndScale
  void *app() const { return app_; }
  void *platform() const { return platform_; }

 private:
  void *platform_ = nullptr, *context_ = nullptr, *app_ = nullptr;
  void *vtable_[kBaseSlots];
};

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/engine.cpp`:
```cpp
#include "engine.h"

#include <cstdlib>

#include "addresses_0_15_10.h"

namespace mcfm {
namespace launcher {
namespace {

template <class F> F fn_at(uintptr_t address) { return reinterpret_cast<F>(address); }
void **vtable_of(void *object) { return *static_cast<void ***>(object); }

}  // namespace

EngineAddresses EngineAddresses::for_slide(uintptr_t slide) {
  EngineAddresses a;
  a.platform_ctor = addr::kFnAppPlatformCtor + slide;
  a.base_vtable = addr::kBaseAppPlatformVtable + slide;
  a.client_ctor = addr::kFnMinecraftClientCtor + slide;
  a.app_init = addr::kFnAppInit + slide;
  a.graphics_vendor = addr::kFnGraphicsVendor + slide;
  a.graphics_renderer = addr::kFnGraphicsRenderer + slide;
  a.graphics_version = addr::kFnGraphicsVersion + slide;
  a.graphics_extensions = addr::kFnGraphicsExtensions + slide;
  return a;
}

bool Engine::start(const EngineAddresses &a, const HostInfo &info, int width, int height) {
  set_host_info(info);
  // AppPlatform: the base constructor (sets the singleton), then our vtable.
  platform_ = std::calloc(1, addr::kAppPlatformSize);
  context_ = std::calloc(1, 16);  // AppContext is an empty object on iOS
  app_ = std::calloc(1, addr::kMinecraftClientSize);
  if (!platform_ || !context_ || !app_) return false;
  fn_at<void (*)(void *)>(a.platform_ctor)(platform_);
  EngineFns fns = {reinterpret_cast<void *>(a.graphics_vendor), reinterpret_cast<void *>(a.graphics_renderer),
                   reinterpret_cast<void *>(a.graphics_version), reinterpret_cast<void *>(a.graphics_extensions)};
  build_vtable(vtable_, reinterpret_cast<void *const *>(a.base_vtable), fns);
  *static_cast<void ***>(platform_) = vtable_;
  // MinecraftClient(argc, argv), App::init(AppContext&), then the sizes (initView).
  fn_at<void (*)(void *, int, char **)>(a.client_ctor)(app_, 0, nullptr);
  fn_at<void (*)(void *, void *)>(a.app_init)(app_, context_);
  resize(width, height);
  return true;
}

void Engine::frame() {
  reinterpret_cast<void (*)(void *)>(vtable_of(app_)[addr::kAppSlotUpdate])(app_);
}

void Engine::resize(int width, int height) {
  reinterpret_cast<void (*)(void *, int, int)>(vtable_of(app_)[addr::kAppSlotSetSize])(app_, width, height);
  reinterpret_cast<void (*)(void *, int, int, float)>(vtable_of(app_)[addr::kAppSlotSetSizeAndScale])(app_, width, height, 0.f);
}

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/seams.h`:
```cpp
#pragma once
// Engine functions the launcher replaces (docs/LAUNCHER.md "Patch policy"). The converter
// patches each address to jump through the hook table; the host fills table[i] with
// hooks()[i].replacement before the game's initializers run. C++11.
#include <cstddef>
#include <cstdint>

namespace mcfm {
namespace launcher {

struct Hook {
  const char *name;
  uintptr_t address;   // unslid, start of the function (>= 12 bytes)
  void *replacement;   // same signature and ABI as the original
};

const Hook *hooks(size_t *count);

}  // namespace launcher
}  // namespace mcfm
```
`shared/launcher/seams.cpp`:
```cpp
#include "seams.h"

#include "addresses_0_15_10.h"

namespace mcfm {
namespace launcher {
namespace {

// std::shared_ptr<T> by value is returned through x8; a type with a user-provided destructor
// gets the same treatment, so this function has the original's ABI.
struct SharedPtrOut {
  void *ptr;
  void *ctrl;
  ~SharedPtrOut() {}
};

// Seam #3: the Xbox Live services config singleton reads xboxservices.config through
// NSBundle (crashes without Foundation). Xbox Live is dropped, so hand out one zeroed
// object: MinecraftClient::init only stores strings into it (zeroed = empty std::string).
SharedPtrOut xbl_app_config() {
  alignas(16) static char config[0x400];
  SharedPtrOut out;
  out.ptr = config;
  out.ctrl = nullptr;
  return out;
}

const Hook kHooks[] = {
    {"xbl_app_config", addr::kFnXblAppConfig, reinterpret_cast<void *>(&xbl_app_config)},
};

}  // namespace

const Hook *hooks(size_t *count) {
  *count = sizeof kHooks / sizeof kHooks[0];
  return kHooks;
}

}  // namespace launcher
}  // namespace mcfm
```

- [ ] **Step 5: Run the test**

Run: `make build/test/launcher_engine_test && build/test/launcher_engine_test`
Expected: `launcher_engine_test: all passed`. Then `make test` → all pass.

- [ ] **Step 6: Commit**

```bash
git add shared/apple/addresses_0_15_10.h shared/launcher/engine.h shared/launcher/engine.cpp shared/launcher/seams.h shared/launcher/seams.cpp shared/tests/launcher_engine_test.cpp Makefile
git commit -m "Launcher: engine boot sequence and the Xbox config seam"
```

---

### Task 5: ANGLE (fetch, verify, provide OpenGLES)

**Files:**
- Create: `tools/launcher/fetch_angle.sh`
- Test: `tools/tests/fetch_angle_test.sh`
- Modify: `Makefile` (`angle` target, test), `macos/tools/make_launcher.sh`

**Interfaces:**
- Produces: `fetch_angle.sh <outdir>` → `<outdir>/libEGL.dylib`, `<outdir>/libGLESv2.dylib` (arm64), from `ANGLE_URL` (default: pinned Electron darwin-arm64 zip) verified against `ANGLE_SHA256`; on any failure exit 1 and leave `<outdir>` without the two files. Inside the zip the files are at `Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries/{libEGL,libGLESv2}.dylib`.
- Produces: `make angle` → `build/angle/`. `make_launcher.sh <app> <outdir> <mcfm-launch> <angle-dir>`: copies ANGLE into `<outdir>` and builds the OpenGLES stub with `--provider OpenGLES=<outdir>/libGLESv2.dylib`.

- [ ] **Step 1: Pin the release (needs the owner's OK to download)**

Ask the owner before downloading. Then pick the latest stable Electron release on https://github.com/electron/electron/releases, download its `SHASUMS256.txt`, and take the line for `electron-v<VERSION>-darwin-arm64.zip`. Record both in the script's defaults (`ANGLE_URL`, `ANGLE_SHA256`). Ledger the version as a ruling.

- [ ] **Step 2: Write the failing test**

`tools/tests/fetch_angle_test.sh`:
```bash
#!/bin/bash
# fetch_angle.sh: extracts the two ANGLE dylibs from a release zip only when the SHA-256
# matches; otherwise leaves nothing behind. Uses a local stand-in zip (no network).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)/with space"; trap 'rm -rf "$(dirname "$T")"' EXIT
LIBS="Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries"
mkdir -p "$T/zip/$LIBS"
for l in libEGL libGLESv2; do
  echo "int $l(void){return 0;}" > "$T/$l.c"
  clang -arch arm64 -dynamiclib "$T/$l.c" -install_name "@rpath/$l.dylib" -o "$T/zip/$LIBS/$l.dylib"
done
(cd "$T/zip" && zip -qry "$T/electron.zip" Electron.app)
SUM="$(shasum -a 256 "$T/electron.zip" | awk '{print $1}')"
fails=0
ANGLE_URL="file://$T/electron.zip" ANGLE_SHA256="$SUM" bash "$ROOT/tools/launcher/fetch_angle.sh" "$T/out" >/dev/null \
  || { echo "FAIL: good archive refused"; fails=$((fails+1)); }
for l in libEGL libGLESv2; do [ -f "$T/out/$l.dylib" ] || { echo "FAIL: $l.dylib missing"; fails=$((fails+1)); }; done
ANGLE_URL="file://$T/electron.zip" ANGLE_SHA256="0000" bash "$ROOT/tools/launcher/fetch_angle.sh" "$T/bad" >/dev/null 2>&1 \
  && { echo "FAIL: wrong checksum accepted"; fails=$((fails+1)); }
ls "$T/bad"/*.dylib >/dev/null 2>&1 && { echo "FAIL: files left after checksum failure"; fails=$((fails+1)); }
[ $fails = 0 ] && echo "fetch_angle_test: passed" || { echo "$fails failure(s)"; exit 1; }
```

- [ ] **Step 3: Run it to verify it fails**

Run: `bash tools/tests/fetch_angle_test.sh` → `FAIL: good archive refused` (script missing).

- [ ] **Step 4: Implement**

`tools/launcher/fetch_angle.sh` (fill `<VERSION>` and `<SHA256>` from Step 1):
```bash
#!/bin/bash
# usage: fetch_angle.sh <outdir>
# ANGLE (OpenGL ES on Metal) for the launcher: libEGL.dylib and libGLESv2.dylib from an
# official Electron macOS arm64 release, verified by SHA-256. Nothing is committed.
set -euo pipefail
OUT="$1"
ANGLE_URL="${ANGLE_URL:-https://github.com/electron/electron/releases/download/v<VERSION>/electron-v<VERSION>-darwin-arm64.zip}"
ANGLE_SHA256="${ANGLE_SHA256:-<SHA256>}"
LIBS="Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/electron.zip" "$ANGLE_URL"
GOT="$(shasum -a 256 "$TMP/electron.zip" | awk '{print $1}')"
[ "$GOT" = "$ANGLE_SHA256" ] || { echo "fetch_angle: checksum mismatch ($GOT, expected $ANGLE_SHA256)" >&2; exit 1; }
unzip -q "$TMP/electron.zip" "$LIBS/libEGL.dylib" "$LIBS/libGLESv2.dylib" -d "$TMP/x"
for l in libEGL libGLESv2; do
  lipo -archs "$TMP/x/$LIBS/$l.dylib" | tr ' ' '\n' | grep -qx arm64 || { echo "fetch_angle: $l has no arm64" >&2; exit 1; }
done
mkdir -p "$OUT"
cp "$TMP/x/$LIBS/libEGL.dylib" "$TMP/x/$LIBS/libGLESv2.dylib" "$OUT/"
echo "fetch_angle: $OUT ready ($(basename "$ANGLE_URL"))"
```
`Makefile` (launcher section):
```make
ANGLE_DIR ?= $(CURDIR)/$(BUILD)/angle
.PHONY: angle
# Downloads ANGLE once (pinned Electron release, ~100 MB); see tools/launcher/fetch_angle.sh.
angle:
	bash tools/launcher/fetch_angle.sh "$(ANGLE_DIR)"
```
In the `launcher:` recipe add a check and the argument:
```make
	@test -f "$(ANGLE_DIR)/libGLESv2.dylib" || { echo "Run make angle first (downloads ANGLE)"; exit 1; }
	bash macos/tools/make_launcher.sh "$(GAME)" "$(LAUNCHER_OUT)" "$(LAUNCHER_BIN)" "$(ANGLE_DIR)"
```
(replacing the old `make_launcher.sh` line). In `macos/tools/make_launcher.sh`: take `ANGLE="$4"`; before `build_stubs.sh` add
```bash
cp "$ANGLE/libEGL.dylib" "$ANGLE/libGLESv2.dylib" "$OUT/"
```
and call `build_stubs.sh "$OUT/imports.tsv" "$OUT" --provider "OpenGLES=$OUT/libGLESv2.dylib"`. Add `bash tools/tests/fetch_angle_test.sh` to `test:`.

- [ ] **Step 5: Run the tests, fetch for real, commit**

Run: `bash tools/tests/fetch_angle_test.sh` → passed; `make test` → all pass. With the owner's OK: `make angle` → `fetch_angle: … ready`. `otool -D build/angle/libGLESv2.dylib` must print `@rpath/libGLESv2.dylib` (if not, ledger a ruling and set it with `install_name_tool -id` in the script, plus a test line).
```bash
git add tools/launcher/fetch_angle.sh tools/tests/fetch_angle_test.sh Makefile macos/tools/make_launcher.sh
git commit -m "Launcher: ANGLE from a pinned Electron release provides OpenGLES"
```

---

### Task 6: `mcfm-launch` window, EGL and frame loop

**Files:**
- Delete: `macos/launcher/main.cpp`; Create: `macos/launcher/main.mm`, `macos/launcher/egl_min.h`, `macos/launcher/resize_math.h`
- Test: `macos/tests/resize_math_test.cpp`; existing `tools/tests/mcfm_launch_test.sh` must keep passing
- Modify: `Makefile` (`LAUNCHER_BIN` rule, `launcher-run`, `launcher-check`, test), `macos/tools/make_launcher.sh` (hooks)

**Interfaces:**
- Consumes: `hooks()`, `Engine`, `EngineAddresses::for_slide`, `HostInfo`, `hook_table_address/capacity`, `is_expected_game_image`.
- Produces: `mcfm-launch [--print-hooks] [--frames N] [image]`:
  - `--print-hooks` prints `hooks.tsv` (`<name>\t0x<address>`) and exits 0 — `make_launcher.sh` feeds it to `mcfm_image.py dylib --hooks`.
  - Exit codes kept from 1a: 2 unreadable/unloadable, 3 not the game. Before `dlopen` it registers an add-image callback that fills the hook table **only if** the image is the game (UUID) and the table capacity ≥ number of hooks.
  - Then: AppKit window 1280×720 (resizable), `CAMetalLayer`, ANGLE EGL display (Metal), ES 3 context, window surface; `Engine::start` with the framebuffer size; a 60 Hz timer per frame: `glBindFramebuffer(GL_FRAMEBUFFER, 0)`, `glViewport`, `engine.frame()`, `eglSwapBuffers`. Resize → `engine.resize(pixel size)`. `--frames N`: print `mcfm: N frames rendered` and exit 0 after N frames.
- Produces: `mcfm::launcher::pixel_size(double points, double scale)` in `resize_math.h` → `int`, at least 1.

- [ ] **Step 1: Write the failing unit test**

`macos/tests/resize_math_test.cpp`:
```cpp
#include "resize_math.h"
#include <cstdio>
static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using mcfm::launcher::pixel_size;
  EXPECT(pixel_size(640, 2.0) == 1280);
  EXPECT(pixel_size(100.4, 1.0) == 100);
  EXPECT(pixel_size(0, 2.0) == 1);
  EXPECT(pixel_size(-5, 2.0) == 1);
  EXPECT(pixel_size(0.2, 1.0) == 1);
  if (fails) return 1;
  std::printf("resize_math_test: all passed\n");
  return 0;
}
```
`Makefile`:
```make
$(BUILD)/test/resize_math_test: macos/tests/resize_math_test.cpp macos/launcher/resize_math.h
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -Wall -O1 -Imacos/launcher macos/tests/resize_math_test.cpp -o $@
```
Add `resize_math_test` to `MACOS_TESTS`. Run: `make build/test/resize_math_test` → FAIL (`resize_math.h` not found).

- [ ] **Step 2: Implement `resize_math.h`**

```cpp
#pragma once
#include <cmath>
namespace mcfm {
namespace launcher {
// Framebuffer pixels for a view dimension in points; never 0 (the engine divides by it).
inline int pixel_size(double points, double scale) {
  double px = std::floor(points * scale);
  return px < 1 ? 1 : static_cast<int>(px);
}
}  // namespace launcher
}  // namespace mcfm
```
Run: `make build/test/resize_math_test && build/test/resize_math_test` → all passed.

- [ ] **Step 3: Write `egl_min.h`**

```cpp
#pragma once
// The few EGL / GLES entry points mcfm-launch uses, loaded from ANGLE with dlsym so the
// launcher builds (and make test runs) without ANGLE. Values from the Khronos registry.
#include <cstdint>

typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;
typedef intptr_t EGLAttrib;

enum : EGLint {
  EGL_NONE = 0x3038, EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022,
  EGL_ALPHA_SIZE = 0x3021, EGL_DEPTH_SIZE = 0x3025, EGL_STENCIL_SIZE = 0x3026,
  EGL_RENDERABLE_TYPE = 0x3040, EGL_OPENGL_ES3_BIT = 0x0040, EGL_CONTEXT_CLIENT_VERSION = 0x3098,
  EGL_PLATFORM_ANGLE_ANGLE = 0x3202, EGL_PLATFORM_ANGLE_TYPE_ANGLE = 0x3203,
  EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE = 0x3489,
};
constexpr unsigned GL_FRAMEBUFFER = 0x8D40;

struct Egl {
  EGLDisplay (*GetPlatformDisplay)(EGLenum, void *, const EGLAttrib *);
  EGLBoolean (*Initialize)(EGLDisplay, EGLint *, EGLint *);
  EGLBoolean (*ChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
  EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
  EGLSurface (*CreateWindowSurface)(EGLDisplay, EGLConfig, void *, const EGLint *);
  EGLBoolean (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
  EGLBoolean (*SwapBuffers)(EGLDisplay, EGLSurface);
  EGLint (*GetError)(void);
  void (*BindFramebuffer)(unsigned, unsigned);
  void (*Viewport)(int, int, int, int);
};
```

- [ ] **Step 4: Write `main.mm`**

`git rm macos/launcher/main.cpp`, then `macos/launcher/main.mm`:
```objc
// mcfm-launch: runs the converted game image (make launcher) in a macOS window.
// usage: mcfm-launch [--print-hooks] [--frames N] [image]
// The LC_UUID is checked on the file before dlopen, the hook table is filled from a dyld
// add-image callback (before the game's initializers), then the engine boots in an ANGLE
// (OpenGL ES 3 on Metal) context. docs/LAUNCHER.md, Stage 1b.
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "egl_min.h"
#include "engine.h"
#include "hook_table.h"
#include "macho_uuid.h"
#include "resize_math.h"
#include "seams.h"

using namespace mcfm::launcher;

namespace {

std::string executable_dir() {
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return ".";
  std::string path(buf);
  return path.substr(0, path.rfind('/'));
}

bool read_header(const std::string &path, std::vector<char> *out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(64 * 1024, 0);
  f.read(out->data(), out->size());
  return f.gcount() >= 32;
}

uintptr_t g_slide = 0;
bool g_found = false;

// dyld calls this after mapping an image and before running its initializers.
void on_add_image(const mach_header *header, intptr_t slide) {
  if (g_found || !mcfm::is_expected_game_image(header)) return;
  g_found = true;
  g_slide = static_cast<uintptr_t>(slide);
  size_t n = 0;
  const Hook *h = hooks(&n);
  uintptr_t table = mcfm::hook_table_address(header);
  if (!table || mcfm::hook_table_capacity(header) < n) {
    std::fprintf(stderr, "mcfm: no hook table in the game image (rebuild with make launcher)\n");
    return;
  }
  void **slots = reinterpret_cast<void **>(table + g_slide);
  for (size_t i = 0; i < n; i++) slots[i] = h[i].replacement;
}

bool load_egl(const std::string &dir, Egl *e) {
  void *egl = dlopen((dir + "/libEGL.dylib").c_str(), RTLD_NOW);
  void *gles = dlopen((dir + "/libGLESv2.dylib").c_str(), RTLD_NOW);
  if (!egl || !gles) { std::fprintf(stderr, "mcfm: cannot load ANGLE: %s\n", dlerror()); return false; }
#define SYM(lib, field, name) e->field = reinterpret_cast<decltype(e->field)>(dlsym(lib, name)); if (!e->field) return false
  SYM(egl, GetPlatformDisplay, "eglGetPlatformDisplay");
  SYM(egl, Initialize, "eglInitialize");
  SYM(egl, ChooseConfig, "eglChooseConfig");
  SYM(egl, CreateContext, "eglCreateContext");
  SYM(egl, CreateWindowSurface, "eglCreateWindowSurface");
  SYM(egl, MakeCurrent, "eglMakeCurrent");
  SYM(egl, SwapBuffers, "eglSwapBuffers");
  SYM(egl, GetError, "eglGetError");
  SYM(gles, BindFramebuffer, "glBindFramebuffer");
  SYM(gles, Viewport, "glViewport");
#undef SYM
  return true;
}

HostInfo host_info(const std::string &game_data_dir) {
  NSString *base = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/MinecraftPE-mcfm"];
  HostInfo info;
  info.data_dir = game_data_dir;
  info.external_dir = std::string(base.UTF8String) + "/external";
  info.internal_dir = std::string(base.UTF8String) + "/internal";
  info.userdata_dir = std::string(base.UTF8String) + "/userdata";
  info.temp_dir = std::string(base.UTF8String) + "/tmp";
  for (const std::string &d : {info.external_dir, info.internal_dir, info.userdata_dir, info.temp_dir})
    [[NSFileManager defaultManager] createDirectoryAtPath:@(d.c_str()) withIntermediateDirectories:YES attributes:nil error:nil];
  info.region = "en_US";
  info.device_id = "mcfm-launcher";
  return info;
}

}  // namespace

@interface McfmView : NSView
@end
@implementation McfmView
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
@end

@interface McfmApp : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic) Egl egl;
@property(nonatomic) EGLDisplay display;
@property(nonatomic) EGLSurface surface;
@property(nonatomic) EGLContext context;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic) long framesLeft;  // < 0: run forever
@property(nonatomic) long framesDone;
@end

@implementation McfmApp {
  Engine _engine;
  std::string _dataDir;
}
- (instancetype)initWithDataDir:(const std::string &)dir frames:(long)frames {
  if ((self = [super init])) { _dataDir = dir; _framesLeft = frames; }
  return self;
}
- (NSSize)pixelSize {
  NSView *v = self.window.contentView;
  double scale = self.window.backingScaleFactor;
  return NSMakeSize(pixel_size(v.bounds.size.width, scale), pixel_size(v.bounds.size.height, scale));
}
- (void)applicationDidFinishLaunching:(NSNotification *)n {
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 720)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
  self.window.title = @"Minecraft PE";
  self.window.delegate = self;
  McfmView *view = [[McfmView alloc] initWithFrame:self.window.contentView.bounds];
  view.wantsLayer = YES;
  self.window.contentView = view;
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  CAMetalLayer *layer = (CAMetalLayer *)view.layer;
  layer.contentsScale = self.window.backingScaleFactor;

  Egl e;
  if (!load_egl(executable_dir(), &e)) { std::fprintf(stderr, "mcfm: ANGLE missing\n"); exit(4); }
  self.egl = e;
  const EGLAttrib display_attribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
  self.display = e.GetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, display_attribs);
  EGLint major = 0, minor = 0;
  if (!self.display || !e.Initialize(self.display, &major, &minor)) { std::fprintf(stderr, "mcfm: eglInitialize failed 0x%x\n", e.GetError()); exit(4); }
  const EGLint config_attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!e.ChooseConfig(self.display, config_attribs, &config, 1, &count) || count < 1) { std::fprintf(stderr, "mcfm: no EGL config\n"); exit(4); }
  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  self.context = e.CreateContext(self.display, config, nullptr, context_attribs);
  self.surface = e.CreateWindowSurface(self.display, config, (__bridge void *)layer, nullptr);
  if (!self.context || !self.surface || !e.MakeCurrent(self.display, self.surface, self.surface, self.context)) {
    std::fprintf(stderr, "mcfm: EGL context/surface failed 0x%x\n", e.GetError());
    exit(4);
  }
  std::printf("mcfm: EGL %d.%d (ANGLE, Metal)\n", major, minor);
  NSSize px = [self pixelSize];
  if (!_engine.start(EngineAddresses::for_slide(g_slide), host_info(_dataDir), (int)px.width, (int)px.height)) exit(5);
  std::printf("mcfm: engine started (%dx%d)\n", (int)px.width, (int)px.height);
  [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 target:self selector:@selector(frame) userInfo:nil repeats:YES];
}
- (void)frame {
  NSSize px = [self pixelSize];
  self.egl.MakeCurrent(self.display, self.surface, self.surface, self.context);
  self.egl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  self.egl.Viewport(0, 0, (int)px.width, (int)px.height);
  _engine.frame();
  self.egl.SwapBuffers(self.display, self.surface);
  self.framesDone++;
  if (self.framesLeft > 0 && --_framesLeft == 0) {
    std::printf("mcfm: %ld frames rendered\n", self.framesDone);
    std::fflush(stdout);
    exit(0);
  }
}
- (void)windowDidResize:(NSNotification *)n {
  NSSize px = [self pixelSize];
  ((CAMetalLayer *)self.window.contentView.layer).drawableSize = CGSizeMake(px.width, px.height);
  _engine.resize((int)px.width, (int)px.height);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
@end

int main(int argc, char **argv) {
  long frames = -1;
  std::string path;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--print-hooks") {
      size_t n = 0;
      const Hook *h = hooks(&n);
      for (size_t k = 0; k < n; k++) std::printf("%s\t0x%lx\n", h[k].name, static_cast<unsigned long>(h[k].address));
      return 0;
    } else if (a == "--frames" && i + 1 < argc) {
      frames = std::atol(argv[++i]);
    } else {
      path = a;
    }
  }
  if (path.empty()) path = executable_dir() + "/libminecraftpe.dylib";
  std::vector<char> header;
  if (!read_header(path, &header)) { std::fprintf(stderr, "mcfm: cannot load %s: unreadable\n", path.c_str()); return 2; }
  if (!mcfm::is_expected_game_image(header.data())) {
    std::fprintf(stderr, "mcfm: %s is not Minecraft PE 0.15.10 (LC_UUID)\n", path.c_str());
    return 3;
  }
  _dyld_register_func_for_add_image(on_add_image);
  if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) { std::fprintf(stderr, "mcfm: cannot load %s: %s\n", path.c_str(), dlerror()); return 2; }
  if (!g_found) { std::fprintf(stderr, "mcfm: %s loaded but not found among dyld images\n", path.c_str()); return 2; }
  std::printf("mcfm: game image loaded (slide 0x%lx)\n", static_cast<unsigned long>(g_slide));
  std::fflush(stdout);
  // The game's data/ lives next to the image's source app: make_launcher.sh writes its path.
  std::string data_dir;
  std::ifstream df(executable_dir() + "/data_dir.txt");
  std::getline(df, data_dir);
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    McfmApp *delegate = [[McfmApp alloc] initWithDataDir:data_dir frames:frames];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
```

- [ ] **Step 5: Build rules, hooks in `make_launcher.sh`, data dir**

Replace the `$(LAUNCHER_BIN)` rule in the `Makefile`:
```make
LAUNCHER_SRCS := macos/launcher/main.mm shared/apple/macho_uuid.cpp shared/apple/hook_table.cpp \
                 shared/launcher/app_platform.cpp shared/launcher/engine.cpp shared/launcher/seams.cpp
LAUNCHER_CXXFLAGS := -arch arm64 -mmacosx-version-min=11.0 -std=c++17 -fobjc-arc -O2 -Wall -Wextra \
                     -Wno-unused-parameter -Ishared/apple -Ishared/launcher -Imacos/launcher

$(LAUNCHER_BIN): $(LAUNCHER_SRCS) $(wildcard shared/launcher/*.h macos/launcher/*.h) shared/apple/hook_table.h shared/apple/macho_uuid.h shared/apple/addresses_0_15_10.h
	@mkdir -p $(dir $@)
	clang++ $(LAUNCHER_CXXFLAGS) $(LAUNCHER_SRCS) -framework AppKit -framework QuartzCore \
	  -Wl,-rpath,@executable_path -o $@
```
In `macos/tools/make_launcher.sh`, replace the `dylib` line with:
```bash
"$BIN" --print-hooks > "$OUT/hooks.tsv"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$TMP/game" "$OUT/libminecraftpe.dylib" --hooks "$OUT/hooks.tsv"
printf '%s/data/\n' "$(cd "$APP" && pwd)" > "$OUT/data_dir.txt"
```
`Makefile` targets:
```make
.PHONY: launcher-run
launcher-run:
	"$(LAUNCHER_OUT)/mcfm-launch"
```
and change `launcher-check` to run `"$(LAUNCHER_OUT)/mcfm-launch" --frames 120` and require both `game image loaded` and `120 frames rendered` in its output (keep the census check).

- [ ] **Step 6: Verify**

Run: `bash tools/tests/mcfm_launch_test.sh` → passed (exit codes 2/3 unchanged); `make test` → all pass.
With game files and ANGLE: `make launcher GAME=… && make launcher-check`.
Expected: `mcfm: EGL 1.5 (ANGLE, Metal)`, `mcfm: engine started`, `mcfm: 120 frames rendered`, `launcher-check: passed`. If the engine crashes before that, go to Task 7 (it is the bring-up loop); commit this task first with `launcher-check` failing only on the real game — record that as a ruling.

- [ ] **Step 7: Commit**

```bash
git add macos/launcher/main.mm macos/launcher/egl_min.h macos/launcher/resize_math.h macos/tests/resize_math_test.cpp macos/tools/make_launcher.sh Makefile
git rm --cached macos/launcher/main.cpp 2>/dev/null; git add -u macos/launcher
git commit -m "Launcher: AppKit window, ANGLE EGL context and frame loop drive the engine"
```

---

### Task 7: Bring-up to a rendered title screen, and docs

**Files:**
- Modify: `shared/launcher/app_platform.cpp`, `shared/launcher/seams.cpp`, `shared/apple/addresses_0_15_10.h` and their tests, as the loop below requires
- Modify: `docs/LAUNCHER.md`, `docs/research/macho-launcher.md`, `docs/HANDOFF.md`, `README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: everything above. Produces: the acceptance state of this plan.

- [ ] **Step 1: The bring-up loop (repeat until acceptance)**

Each iteration:
1. Run `make launcher-check` (real game; `make launcher` first after code changes).
2. If it crashes: run the image under lldb — `lldb --batch -o run -o "bt 30" -k "bt 30" -- dist/launcher/mcfm-launch --frames 120` — and read the frames: lldb names unknown functions `___lldb_unnamed_symbol_<unslid address>`; decompile them with `tools/ida/q.py`.
3. Classify the cause and apply exactly one fix, test first:
   - **A pure/base AppPlatform slot misbehaves** (e.g. returns empty data, wrong type): implement it in `app_platform.cpp` with a new `EXPECT` in `launcher_app_platform_test.cpp` that fails first.
   - **An iOS-glue or dropped-component function is reached directly** (a seam): add a `Hook` in `seams.cpp` with the original's ABI and a minimal replacement, a test in `launcher_engine_test.cpp` calling it through a function pointer, and the address in `addresses_0_15_10.h` (verify the function is ≥ 12 bytes in IDA).
   - **A stub returns 0 where the engine needs a value** (from `build/launcher/census.txt`): if it is a framework the patch policy drops, hook the caller instead of the stub; never implement Apple framework APIs in stubs.
4. Ledger one line per iteration: crash site, cause, fix, test.

Acceptance: `make launcher-check` passes (120 frames) **and** `make launcher-run` shows the title screen (ask the owner to confirm with a screenshot, or take one with `screencapture -l<windowid>`). Stop and report if an iteration needs a decision the spec does not cover (e.g. sharing worlds with the Catalyst build).

- [ ] **Step 2: Docs**

- `docs/research/macho-launcher.md`: add a section "Stage 1b findings" — the slots the engine calls during boot (from the slot log), the seams hooked so far and why, `getSystemRegion` must be `language_REGION`, base `getAssetFileFullPath` returns the relative path (the launcher prefixes `data/`), the graphics getters are engine functions, the hook-table mechanism and why runtime text patching is impossible on Apple Silicon.
- `docs/LAUNCHER.md`: Stage 1 items 3 (GL), 4 (AppPlatform), 5 (boot and frame loop) ☑; 1b ☑ with the plan link; Decisions: ANGLE source (Electron version), launcher storage directory, hook table.
- `docs/HANDOFF.md` §3 (`make angle`, `make launcher-run`), §6.0 status.
- `README.md` launcher paragraph: `make angle` (downloads ~100 MB once), `make launcher`, `make launcher-run`.
- `CLAUDE.md` Commands: `make angle`, `launcher-run`.

- [ ] **Step 3: Final verification and commit**

Run: `make test` → all pass; `make launcher-check` → passed.
```bash
git add -A shared macos tools docs README.md CLAUDE.md Makefile
git commit -m "Launcher: the engine boots to the title screen; Stage 1b docs"
```
