# Mach-O launcher — plan

Goal: run the **iOS arm64 Mach-O** of Minecraft PE 0.15.10 anywhere we write a launcher for,
the way mcpelauncher runs the Android `.so` on Linux. The iOS glue in the binary (UIKit view
controller, `AppPlatform_iOS`, EAGL, StoreKit, Xbox Live UI) is **never executed**; our launcher
loads the image, provides libc/libc++/GL/audio, implements `AppPlatform` itself and drives the
engine. First macOS, then Android, later any arm64 system.

Survey results behind this plan: [research/macho-launcher.md](research/macho-launcher.md).
Status legend: ☐ to do · ◐ in progress · ☑ done.

## Why it is feasible
- ✅ The engine (`0x100000000–0x1006FFFFF`, 45k functions) uses no Apple framework at all; its
  outside needs are libc, libc++ and OpenGL ES 3.
- ✅ It reaches platform code only through the `AppPlatform` vtable (slot map in
  [research/appplatform-vtable.md](research/appplatform-vtable.md)) and 9 direct seams (HTTP,
  store, Xbox Live services, Xbox TCUI).
- ✅ The format is simple to load: a PIE arm64 executable with classic dyld opcode fixups,
  no TLS and no pointer authentication.
- ✅ The boot sequence is short and known: base `AppPlatform` ctor → `MinecraftClient` ctor →
  `App::init` → sizes → per frame `update()`.

## Limits (accepted)
- **arm64 hosts only**: Apple Silicon Macs, arm64 Android, arm64 Linux, Windows on ARM. x86
  hosts would need CPU emulation (out of scope).
- **Users supply a decrypted IPA.** We never redistribute Mojang files (`no_game_files_test`).
- Xbox Live / Realms do not work in these copies anyway: stubbed to "signed out / offline".

## Architecture

```
launcher (per platform: macOS app, Android APK, …)
 ├─ loader        maps the Mach-O, applies fixups, resolves imports, runs initializers,
 │                registers unwind info                         (Stage 1: Apple's dyld; Stage 2+: ours)
 ├─ host runtime  what the imports bind to:
 │   ├─ libc      host libc on Apple; a Darwin→host translation layer elsewhere
 │   ├─ libc++    host libc++ on Apple; a libc++ built with Apple's ABI elsewhere
 │   ├─ GL        ANGLE or native GLES 3 behind the `gl*` imports
 │   ├─ audio     the AudioToolbox subset FMOD uses, on CoreAudio / AAudio
 │   └─ stubs     every other framework import: logs the first call, returns "failed/empty"
 ├─ platform      our AppPlatform (shared C++ over the 103-slot map), window, input,
 │                text input, file paths, HTTP, the engine boot and frame loop
 └─ mods          today's features (win10_ui, keyboard_mouse, …) on top of the platform
```

`shared/` grows a `launcher/` part (platform-free C++11: boot sequence, AppPlatform policy,
stub tables), with per-host code under `macos/` and `android/`. The current Catalyst build
stays the working macOS version until Stage 1 replaces it.

## Stage 0 — survey ☑
Done 2026-10-10: imports, where Apple APIs are used, seams, boot sequence, layout, unwinding,
string ABI. Tool: `tools/ida/survey.py`. Results: [research/macho-launcher.md](research/macho-launcher.md).

## Stage 1 — macOS launcher on Apple's loader ☐
Let dyld do the loading so we can focus on the platform layer, the boot and rendering.

1. ☐ **Image preparation** (`tools/` script, run at install time like `convert.sh`): copy the
   binary; `MH_EXECUTE` → `MH_DYLIB` with an `LC_ID_DYLIB`; drop `LC_MAIN`; retag the platform
   to macOS (`LC_BUILD_VERSION`); point each framework's `LC_LOAD_DYLIB` at our stub library;
   ad hoc sign. Test with a small arm64 iOS executable we compile ourselves (no game files).
2. ☐ **Stub libraries**, generated from the import list (Apple API names only, safe to
   commit): functions log `mcfm: unimplemented <lib>:<symbol>` once and return 0/NULL; data
   symbols are zeroed; ObjC classes the image subclasses (`UIViewController`, `UIView`, …) are
   empty `NSObject` subclasses. Test: every import of the reference list resolves.
   **Decision needed**: for frameworks macOS also has (Foundation, CoreFoundation, Security,
   CFNetwork, AudioToolbox, …), either bind to the real ones (faster; Xbox/HTTP glue may just
   work) or stub them too (Stage 1 then already proves the "no Apple frameworks" claim).
   Recommendation: stub everything except libSystem, libc++, libobjc and libz, and fix the
   seams instead — it is what every other host needs anyway.
3. ☐ **GL**: bind the 84 engine `gl*` imports to ANGLE (GLES 3 on Metal). ANGLE builds or
   prebuilt binaries are the open question; shaders ship as GLSL ES in the game data.
4. ☐ **Our AppPlatform**: call the base ctor `0x10045F678` on a 360+ byte object, then set the
   vptr to our own vtable: a copy of the base vtable `0x100E649C0` with our overrides (Win10
   edition, UI scaling rules, input mode, pointer show/hide, keyboard, paths, …). This is
   step 4 of the generic AppPlatform plan in [HANDOFF.md §6.1](HANDOFF.md#61-generic-appplatform).
   Every slot gets a FakePlatform host test.
5. ☐ **Boot and frame loop**: `MinecraftClient` ctor `0x10006E2DC`, `AppContext`, `App::init`
   `0x1000555BC`, `setRenderingSize` / `setUISizeAndScale`, then per frame: bind the default
   framebuffer → run main-thread jobs → `update()` → present. Window via AppKit +
   `CAMetalLayer` (ANGLE surface).
6. ☐ **Seams**: replace HTTP (#1) with our own client or a "no network" stub, store (#2) with
   "no products", Xbox services (#3) and TCUI (#4) with "signed out". Hook on Stage 1's mapped
   image by patching the call sites or the target entry points.
7. ☐ **Input**: reuse `shared/` keyboard/mouse (it writes the engine's `Keyboard`/`Mouse`
   queues directly) and pointer capture.
8. ☐ **Audio**: FMOD's output uses a RemoteIO AudioUnit (iOS only) and `AudioSession*`: shim
   `AudioComponentFindNext` to the macOS default output and stub the session calls.
9. ☐ **Runtime census**: play a session (menus, world creation, gameplay, chat, settings) and
   collect every `unimplemented` log line → the real shim list for Stages 2–3.

Acceptance: `make launcher` builds `dist/MinecraftPE.app` (no Catalyst, no UIKit) that
reaches the title screen and plays a world with keyboard, mouse, sound and resizing; `make
test` covers the image prep, the stub tables and every AppPlatform slot.

## Stage 2 — our own loader, still on macOS ☐
Replace dyld so the same loader can later run on Android/Linux, debugged where tools are best.

1. ☐ Parse load commands; `mmap` an anonymous region (`__PAGEZERO` excluded), copy segments in,
   apply `LC_DYLD_INFO_ONLY` rebase/bind/lazy-bind/weak-bind opcodes, `mprotect` to the segment
   protections.
2. ☐ Import resolution through our own symbol table (one table per host), with the stub
   fallback from Stage 1.
3. ☐ Run the 3,972 static initializers (`__mod_init_func`) in order; give ObjC metadata no
   runtime (the glue never runs; `objc_autoreleasePoolPush/Pop` in about 19 initializers are
   no-op stubs).
4. ☐ **Exceptions**: make the unwinder find our image's `__unwind_info`/`__eh_frame`
   (macOS: `__unw_add_find_dynamic_unwind_sections` ❓ availability; otherwise our own
   LLVM libunwind build). Test: throw and catch across a fixture image.
5. ☐ Keep the LC_UUID guard: the loader exposes the slide so `addresses_0_15_10.h` keeps working.

Acceptance: Stage 1's launcher runs the game through our loader; fixture tests (our own
compiled arm64 Mach-O files) cover fixups, imports, initializers and exceptions.

## Stage 3 — Android ☐
1. ☐ **Target SDK 28** (decided 2026-10-10: 29+ is too restrictive for us and for runet-style
   injection). Distribution is sideload-only (Play requires newer target SDKs). The loader
   still maps the image into anonymous memory and `mprotect`s it to RX, which also works on
   API 29+.
2. ☐ **libc translation layer** (Darwin ABI → bionic), the large item:
   - struct layouts: `stat`/`fstat`/`lstat`, `dirent`/`readdir`, `pthread_*_t` sizes and
     static initializers (`PTHREAD_MUTEX_INITIALIZER` signatures), `locale_t`;
   - stdio globals `__stderrp`/`__stdinp`, `FILE*` used only opaquely ❓;
   - `errno` via `___error` and the different errno numbers;
   - `open`/`fcntl`/`mmap` flags, `O_*`, `F_*`, socket options;
   - sockets: Darwin `sockaddr` has `sa_len`/`sin_len`; `addrinfo` member order differs
     (`ai_canonname`/`ai_addr` swapped);
   - Darwin-only APIs used by the engine: `__DefaultRuneLocale` (ctype tables),
     `__sincosf_stret`/`__sincos_stret`, `memset_pattern16`; by libraries: `mach_*`
     time/semaphores, `dispatch_*`, `kqueue`, `OSMemoryBarrier`, blocks, CommonCrypto (only
     needed if the Xbox/telemetry code runs — it shouldn't).
3. ☐ **libc++** built for Android with ABI v1 + `_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`,
   exporting the symbol names the binary imports (285); plus libc++abi and libunwind with
   compact unwind support.
4. ☐ GL: native GLES 3 via EGL (or ANGLE on Vulkan); audio: the FMOD AudioToolbox subset on
   AAudio/OpenSL ES.
5. ☐ Android platform layer: `NativeActivity`-style launcher, input, text input, file paths,
   IPA import screen.

Acceptance: an APK that installs on Android 6+ arm64, imports a user-supplied IPA and plays.

## Later
- Linux arm64 / Windows on ARM launchers (Stage 2 loader + Stage 3 translation, different host).
- Our own renderer behind the `gl*` imports ([research/renderer.md](research/renderer.md)).
- Module system on top of the launcher's platform layer (HANDOFF §6.3).

## Open decisions (owner)
- Stage 1 binding policy for frameworks macOS also has (see Stage 1.2).
- ANGLE: build from source or use prebuilt binaries? Where do they live (git-ignored, fetched
  by a script)?
- How users supply the IPA: the launcher imports it (needs an IPA picker) or `make` targets only.
- Keep the Catalyst build after Stage 1 (iOS mod path) or retire it for macOS.
