# Stage 2 — Our Own Mach-O Loader (macOS) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mcfm-launch` loads the converted game image with our own loader instead of Apple's dyld — mapping, fixups, initializers, unwind registration, hooks — proven equivalent to dyld on the real game (every `__DATA` pointer compared), and the game plays with it; the loader core is platform-free so Stage 3 (Android) reuses it.

**Architecture:** `shared/loader/` (C++11, no OS headers): `macho_file` parses a Mach-O from bytes; `fixups` decodes the classic `LC_DYLD_INFO_ONLY` rebase / bind / lazy-bind / weak-bind opcode streams and the export trie exactly as dyld does; `loader` maps an image through a small `LoaderOS` interface, applies the fixups with a symbol resolver, fills the hook table and runs the initializers. `macos/launcher/loader_macos.mm` implements `LoaderOS` with `mmap` + `F_ADDFILESIGS_RETURN` (file-backed, signed code), `dlopen`/`dlsym`, and `__unw_add_find_dynamic_unwind_sections`. `mcfm-launch --loader own|dyld` selects the loader; `make loader-check` loads the game both ways in one process and compares every `__DATA` word.

**Tech Stack:** C++11 (shared), Objective-C++17 (macOS), clang/ld (fixtures), bash, make.

**Spec:** `docs/LAUNCHER.md` Stage 2 (items 1–5, acceptance). Spike results (2026-10-10, scratchpad, recorded in Task 6's docs): file-backed mapping after `F_ADDFILESIGS_RETURN` executes the image's code; `__unw_add_find_dynamic_unwind_sections` makes C++ exceptions inside the image work (thrown 5 frames deep through destructors, caught); `dyld_info -fixups` text is **not** a faithful fixup source for the game (it ends with binds that rebind lazy-pointer slots to unrelated symbols), so the loader decodes the opcode streams itself.

## Global Constraints

- All launcher constraints hold (no Mojang files committed; `make test` without game files or ANGLE; host libraries `libSystem`, `libc++`, `libz`; logs `mcfm:`; `make app/check/run` refuse while `mcfm-launch` runs).
- `shared/loader/` is C++11 with no OS headers (no `<sys/mman.h>`, `<dlfcn.h>`, Mach-O SDK headers): constants are defined locally; the OS comes in through `LoaderOS`.
- Only the classic format is supported (`LC_DYLD_INFO(_ONLY)`, no chained fixups, no TLV, 64-bit arm64); anything else is refused with a clear error.
- Fixup semantics follow dyld: rebases first, then binds, then lazy binds (bound eagerly), then weak binds; weak definitions coalesce to the first definition in load order, i.e. a host library's export wins over the image's own weak definition (observed: `operator new/delete` bind to libc++ under dyld).
- `--loader dyld` stays available until the owner has played with `--loader own`; then `own` becomes the default.
- Fixtures are built by the tests (clang, `-Wl,-no_fixup_chains`, retagged iOS like `launcher_fixture.sh`); no binary fixtures are committed.

## Review Focus

1. A malformed or hostile image (opcode stream overruns, segment index out of range, fixup address outside a writable segment) → load fails with an error, never writes outside the image (Task 2/3 tests with crafted streams).
2. Weak symbols: the same choice as dyld (host first) — a mismatch would split `operator new`/`delete` between allocators (Task 5 equivalence on the real game; Task 3 test).
3. An initializer that throws or calls a hooked function → the hook table is filled before initializers, and exceptions inside initializers unwind (Task 4 fixture).
4. Two images loaded in one process (`loader-check` loads the game via dyld and via ours) → the unwind callback and hook filling address only our image's range (Task 4/5).
5. Missing symbol in a non-weak import → load fails naming the library and symbol; a weak import resolves to 0 (Task 3 test).

---

## File Structure

| File | Responsibility |
|---|---|
| `shared/loader/macho_file.h/.cpp` | parse header, segments, sections, dylibs, dyld info, code signature, UUID from bytes (bounds-checked) |
| `shared/loader/fixups.h/.cpp` | decode rebase/bind/lazy/weak opcode streams → `Fixup` list; export trie lookup |
| `shared/loader/loader.h/.cpp` | `LoaderOS`, `Image`, `load_image()`: map, resolve, apply, hooks, initializers |
| `macos/launcher/loader_macos.h/.mm` | `MacLoaderOS`: mmap + code signature, dlopen/dlsym, unwind registration |
| `macos/launcher/main.mm` | `--loader own|dyld`; image header/slide from our loader |
| `shared/tests/macho_fixups_test.cpp` | opcode decoder unit tests on crafted byte streams |
| `tools/tests/loader_fixture.sh`, `tools/tests/loader_test.sh` | fixtures (initializer, exception, weak def, lazy/weak binds, addend) and loader tests vs dyld |
| `macos/tools/loader_check.mm` + `make loader-check` | real game: dyld vs ours, every `__DATA` word |
| `docs/LAUNCHER.md`, `docs/research/macho-launcher.md`, `docs/HANDOFF.md`, `CLAUDE.md` | Stage 2 status, findings |

Branch: `claude/loader-2` (created from `main`).

---

### Task 1: Mach-O file parser

**Files:** create `shared/loader/macho_file.h/.cpp`, `shared/tests/macho_file_test.cpp`; modify `Makefile`.

**Interfaces:**
- Produces (`namespace mcfm::loader`):
```cpp
struct Section { std::string segment, name; uint64_t addr, size; uint32_t offset, flags; };
struct Segment { std::string name; uint64_t vmaddr, vmsize, fileoff, filesize; uint32_t maxprot, initprot; std::vector<Section> sections; };
struct DyldInfo { uint32_t rebase_off, rebase_size, bind_off, bind_size, weak_bind_off, weak_bind_size, lazy_bind_off, lazy_bind_size, export_off, export_size; };
struct MachOFile {
  uint32_t filetype = 0, flags = 0;
  std::vector<Segment> segments;            // in load-command order (fixups index them)
  std::vector<std::string> dylibs;          // LC_LOAD_*DYLIB / REEXPORT in order: ordinal i+1
  std::vector<bool> weak_dylibs;            // LC_LOAD_WEAK_DYLIB
  bool has_dyld_info = false, has_chained_fixups = false;
  DyldInfo dyld_info = {};
  uint32_t code_signature_off = 0, code_signature_size = 0;
  uint8_t uuid[16] = {};
  const Section *section(const char *segment, const char *name) const;
  const Segment *segment(const char *name) const;
};
// false + message on anything malformed (bounds, cmdsize, nsects, offsets past the file).
bool parse_macho(const uint8_t *data, size_t size, MachOFile *out, std::string *error);
```

- [ ] **Step 1: Write the failing test** — `shared/tests/macho_file_test.cpp` builds no fixture; it crafts headers in memory (like `hook_table_test.cpp`): one `__TEXT` segment with `__text` and `__unwind_info` sections, `__DATA` with `__mod_init_func`, `LC_DYLD_INFO_ONLY`, two `LC_LOAD_DYLIB` (one weak), `LC_CODE_SIGNATURE`, `LC_UUID`; asserts every field; then three malformed variants (cmdsize 0; `nsects` past cmdsize; `dyld_info.bind_off + bind_size` past the file size) each return false with a non-empty error; a chained-fixups header (`LC_DYLD_CHAINED_FIXUPS` = 0x80000034) sets `has_chained_fixups`. Add the Makefile rule (`clang++ -std=c++11 -Wall -Wextra -O1 -Ishared/loader ... -fsanitize=address`) and add `macho_file_test` to `SHARED_TESTS`. Run → FAIL (header missing).
- [ ] **Step 2: Implement** `macho_file.cpp` with local constants (`LC_SEGMENT_64 0x19`, `LC_DYLD_INFO 0x22`, `LC_DYLD_INFO_ONLY 0x80000022`, `LC_LOAD_DYLIB 0xC`, `LC_LOAD_WEAK_DYLIB 0x80000018`, `LC_REEXPORT_DYLIB 0x8000001F`, `LC_LAZY_LOAD_DYLIB 0x20`, `LC_LOAD_UPWARD_DYLIB 0x80000023`, `LC_CODE_SIGNATURE 0x1D`, `LC_UUID 0x1B`, `LC_DYLD_CHAINED_FIXUPS 0x80000034`), reading with `memcpy` (no alignment assumptions), checking every offset against `size`. Run → pass.
- [ ] **Step 3: Commit** — `make test` green; `git commit -m "Loader: Mach-O file parser"`.

---

### Task 2: Fixup decoder (opcode streams + export trie)

**Files:** create `shared/loader/fixups.h/.cpp`, `shared/tests/macho_fixups_test.cpp`; modify `Makefile`.

**Interfaces:**
```cpp
enum class FixupKind { Rebase, Bind, LazyBind, WeakBind };
struct Fixup {
  FixupKind kind;
  int segment;            // index into MachOFile::segments
  uint64_t offset;        // within the segment
  int ordinal;            // bind: 1..N dylib, 0 self, -1 main executable, -2 flat lookup
  std::string symbol;     // with the leading '_'
  int64_t addend;
  bool weak_import;       // BIND_SYMBOL_FLAGS_WEAK_IMPORT (missing -> 0)
};
// Decodes all four streams in dyld's order: rebases, binds, lazy binds, weak binds.
bool decode_fixups(const uint8_t *file, size_t size, const MachOFile &m, std::vector<Fixup> *out, std::string *error);
// Export trie lookup: offset of `symbol` from the image base (vmaddr of __TEXT), flags.
bool find_export(const uint8_t *file, size_t size, const MachOFile &m, const std::string &symbol, uint64_t *offset, uint32_t *flags);
```
Opcode semantics (dyld, `mach-o/loader.h`): REBASE — `0x00` DONE, `0x10` SET_TYPE_IMM, `0x20` SET_SEGMENT_AND_OFFSET_ULEB (seg = imm), `0x30` ADD_ADDR_ULEB, `0x40` ADD_ADDR_IMM_SCALED (+imm·8), `0x50` DO_REBASE_IMM_TIMES (imm × {rebase; +8}), `0x60` DO_REBASE_ULEB_TIMES, `0x70` DO_REBASE_ADD_ADDR_ULEB ({rebase; +uleb+8}), `0x80` DO_REBASE_ULEB_TIMES_SKIPPING_ULEB (count × {rebase; +skip+8}). BIND — `0x00` DONE (ends the regular/weak stream; in the **lazy** stream it only ends one entry — keep decoding to the end), `0x10` SET_DYLIB_ORDINAL_IMM, `0x20` SET_DYLIB_ORDINAL_ULEB, `0x30` SET_DYLIB_SPECIAL_IMM (imm sign-extended from 4 bits), `0x40` SET_SYMBOL_TRAILING_FLAGS_IMM (C string follows; flags imm: 0x1 weak import, 0x8 non-weak definition), `0x50` SET_TYPE_IMM, `0x60` SET_ADDEND_SLEB, `0x70` SET_SEGMENT_AND_OFFSET_ULEB, `0x80` ADD_ADDR_ULEB, `0x90` DO_BIND (+8), `0xA0` DO_BIND_ADD_ADDR_ULEB (+uleb+8), `0xB0` DO_BIND_ADD_ADDR_IMM_SCALED (+imm·8+8), `0xC0` DO_BIND_ULEB_TIMES_SKIPPING_ULEB, `0xD0` THREADED → error (unsupported). Weak-bind entries with flag 0x8 (strong definition, no address) record nothing. Every fixup must lie inside its segment (`offset + 8 <= vmsize`) and the segment must be writable, else error.

- [ ] **Step 1: Failing tests on crafted streams** — `macho_fixups_test.cpp` builds a `MachOFile` in memory (two segments: `__TEXT` r-x, `__DATA` rw) and byte buffers for each opcode above: e.g. rebase `{0x11, 0x21, 0x10, 0x52, 0x00}` → two rebases at DATA+0x10/+0x18; bind with SLEB addend `-8`; lazy stream with two entries separated by DONE; ULEB-times-skipping; special ordinal −2; weak import flag; THREADED → error; DO_BIND past the segment end → error; segment index 5 → error; export trie with two symbols (node with terminal info, child edges) → `find_export` finds both, misses a third. Run → FAIL.
- [ ] **Step 2: Implement** with bounds-checked `uleb128`/`sleb128` readers and a recursive-free trie walk (stack depth ≤ 128). Run → pass.
- [ ] **Step 3: Cross-check on fixtures** — `tools/tests/loader_fixture.sh <outdir>` builds `fixture` (thin arm64, iOS-tagged, `-no_fixup_chains`) with: a lazily bound libc call, a GOT-bound data import with addend (`&kFakeKitValue[8]`), a weak definition that libc++ also exports (`operator new` override defined `__attribute__((weak))`), a weak import of a missing symbol (`__attribute__((weak_import))` from FakeKit, not provided), an initializer, and `thrower()` (as the spike). A small decoder tool `build/test/fixups_dump <file>` prints fixups in `dyld_info -fixups` form; `tools/tests/loader_test.sh` compares its output with `dyld_info -fixups` **for the fixture** (clean there) after normalising order. Run → pass. Commit `"Loader: rebase/bind/lazy/weak decoder and export trie"`.

---

### Task 3: Loader core (`LoaderOS`, map, resolve, apply, hooks, initializers)

**Files:** create `shared/loader/loader.h/.cpp`, `shared/tests/loader_core_test.cpp`; modify `Makefile`.

**Interfaces:**
```cpp
struct LoaderOS {  // implemented per host; a fake one in tests
  virtual ~LoaderOS() {}
  virtual uint8_t *reserve(size_t size) = 0;                                    // PROT_NONE region
  virtual bool register_code_signature(int fd, uint64_t off, uint64_t size) = 0;
  virtual bool map_file(uint8_t *at, size_t size, int prot, int fd, uint64_t off) = 0;
  virtual bool map_zero(uint8_t *at, size_t size, int prot) = 0;
  virtual void *open_library(const std::string &name) = 0;                      // short name -> handle
  virtual void *symbol(void *library, const std::string &name) = 0;             // name without '_'
  virtual void *flat_symbol(const std::string &name) = 0;                       // any loaded library
  virtual void register_unwind(uintptr_t header, uintptr_t text_lo, uintptr_t text_hi,
                               uintptr_t compact_unwind, size_t compact_size, uintptr_t eh_frame, size_t eh_size) = 0;
  virtual void log(const std::string &line) = 0;
};
struct Image { uint8_t *header; intptr_t slide; uintptr_t text_lo, text_hi; MachOFile macho; };
struct LoadOptions {
  const void *const *hook_replacements = nullptr; size_t hook_count = 0;   // filled into the hook table before initializers
  bool run_initializers = true;
};
// fd/file: the converted image (bytes for parsing, fd for mapping). false + error on failure.
bool load_image(LoaderOS &os, int fd, const uint8_t *file, size_t size, const LoadOptions &opts, Image *out, std::string *error);
```
Resolution: ordinal ≥ 1 → `os.symbol(os.open_library(short_name(dylibs[ordinal-1])), sym)`; 0 (self) → own export (`find_export`, + slide); −2 → `os.flat_symbol`; weak binds → host first (`os.flat_symbol`), else own export. Missing non-weak symbol → error naming library and symbol; weak import → 0. Short names as in `mcfm_image.py` (`libc++`, `libSystem`, `libz`, `mcfm_stub_<lib>`). Hook table: same rule as `shared/apple/hook_table.cpp` (reuse `hook_table_address`/`capacity` on the mapped header). Initializers: every `S_MOD_INIT_FUNC_POINTERS` (pointers, already rebased) and `S_INIT_FUNC_OFFSETS` (32-bit offsets from the header) section, in order, called `(argc, argv, envp, apple, vars)` with 0/nullptr.

- [ ] **Step 1: Failing tests with a fake OS** — `loader_core_test.cpp`: `FakeOS` backs `reserve` with `calloc`, `map_file` with `memcpy` from the file bytes (ignores prot), resolves libraries from a table of fake symbols. Using the crafted image from Task 2 plus a real fixture read from disk (path argument from the test script): after `load_image(run_initializers=false)`, each rebased slot equals target + slide, each bound slot equals the fake symbol (+ addend), the weak import is 0, the weak definition resolves to the host's fake when present and to the own export when not, a missing non-weak symbol fails with "FakeKit: _missing", and the hook table holds the replacements. Run → FAIL.
- [ ] **Step 2: Implement** `loader.cpp`. Run → pass. Commit `"Loader: core — map, resolve, apply, hooks, initializers"`.

---

### Task 4: macOS `LoaderOS` and running real code

**Files:** create `macos/launcher/loader_macos.h/.mm`; extend `tools/tests/loader_test.sh`; modify `Makefile`.

**Interfaces:** `class MacLoaderOS : public mcfm::loader::LoaderOS` — `reserve` = `mmap(PROT_NONE, MAP_PRIVATE|MAP_ANON)`; `register_code_signature` = `fcntl(fd, F_ADDFILESIGS_RETURN, &fsignatures_t{0, (void *)off, size})`; `map_file` = `mmap(MAP_PRIVATE|MAP_FIXED)`; `map_zero` = anonymous `MAP_FIXED`; libraries: `libc++`/`libSystem`/`libz` → their `/usr/lib` paths, others → `<dir of the image>/<name>.dylib`; `flat_symbol` = `dlsym(RTLD_DEFAULT, …)`; `register_unwind` = one `__unw_add_find_dynamic_unwind_sections` callback serving every registered range (a small table, so two images never collide).

- [ ] **Step 1: Failing test** — `loader_test.sh` builds `build/test/loader_run` (a C++ host linking `loader.cpp`, `fixups.cpp`, `macho_file.cpp`, `loader_macos.mm`) and runs it on the converted fixture: expects the initializer to have run, `thrower()` (found via the fixture's symbol offset passed on the command line) to return 42 (exception caught inside the image), `operator new` bound to libc++'s, and the hook (fixture's `fixture_answer`) to be replaced. Run → FAIL.
- [ ] **Step 2: Implement** `loader_macos.mm`. Run → pass. Commit `"Loader: macOS OS layer — signed file mapping, symbols, unwind"`.

---

### Task 5: Equivalence with dyld on the real game (`make loader-check`)

**Files:** create `macos/tools/loader_check.mm`; modify `Makefile`.

- [ ] **Step 1: Implement the check** — `build/launcher/loader-check <image>`: `dlopen`s the image (dyld, `RTLD_NOW`, hooks not filled, initializers run by dyld — acceptable: they ran fine under dyld before) and loads the same file with our loader **without** running initializers; then for every 8-byte word of the file-backed part of `__DATA` compares `ours` with `dyld`: equal, or both point into their own image at the same unslid address. Words written by dyld's initializers (`__bss`/`__common` are zero-fill, not compared; initializers write `__data` statics too) are excluded by comparing only words that are fixup locations (from our decoder). Prints the count and the first 20 differences with symbol names; exit 1 on any difference. `make loader-check` builds and runs it on `dist/launcher/libminecraftpe.dylib`.
- [ ] **Step 2: Run on the game** — expected "N fixups equal (rebases R, binds B), 0 differences". Each difference is a decoder/resolver bug: fix it test-first in Task 2/3's tests (crafted stream reproducing it), ledger it, rerun until 0.
- [ ] **Step 3: Commit** `"Loader: loader-check — every game fixup equals dyld's"`.

---

### Task 6: `mcfm-launch --loader own`, play, default, docs

**Files:** modify `macos/launcher/main.mm`, `Makefile`, `tools/tests/mcfm_launch_test.sh`, docs.

- [ ] **Step 1: Failing test** — `mcfm_launch_test.sh`: `--loader bogus` → exit 2 usage; `--loader own` on the fixture image → exit 3 (UUID check still first). Run → FAIL.
- [ ] **Step 2: Implement** — parse `--loader own|dyld` (default `dyld` for now); `own`: read the file, `load_image` with the hooks from `hooks()` (no add-image callback), `g_slide`/header from `Image`; the UUID check, data dir check and everything after stay the same. `make check-own` = `launcher-check` with `--loader own`.
- [ ] **Step 3: Verify** — `make test`; `make app && make check-own` → 120 frames; then the owner plays with `make run LOADER=own` (world, chat, sound, quit-save). Fix issues test-first.
- [ ] **Step 4: Default** — after the owner's OK: default `own`, keep `--loader dyld`; `make check` uses own; update tests.
- [ ] **Step 5: Docs** — `docs/LAUNCHER.md` Stage 2 ☑ items 1–5 + acceptance; `docs/research/macho-launcher.md` "Stage 2 findings" (spike results above, dyld_info caveat, weak-bind rule, loader-check result); HANDOFF §6.0 + daily commands; CLAUDE.md (loader layout, `--loader`). Commit `"Stage 2: our own loader runs the game (default)"`.
