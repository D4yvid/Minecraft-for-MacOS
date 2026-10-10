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
- CLI only for now: no launcher UI; the IPA is imported by `make` targets.

## Patch policy
We control every import binding and may patch any call site (addresses are only valid for
0.15.10 behind the LC_UUID guard), so anything outside the engine is replaced or dropped:

| Component | Policy |
|---|---|
| iOS glue (view controller, `AppPlatform_iOS`, EAGL, keyboard view, game controllers) | never run; our launcher and AppPlatform replace it |
| FMOD | kept; its AudioToolbox imports bind to **our wrappers** with the same API, which do the platform call (CoreAudio, AAudio, …) — no code patch. Includes AudioQueue/AudioFile if the census shows them used |
| Microsoft account, Xbox Live, TCUI, telemetry HTTP | **dropped**: seams #3/#4 patched to "signed out"/no-op, imports stubbed; their code stays in the image unused |
| HTTP (seam #1) | patched: "no network" first, our own client later if something needs it |
| Store (seam #2) | patched: "no products" |
| Every other framework import | stub (log first call, return failure/empty) |

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
stub tables), with per-host code under `macos/` and `android/`. The Mac Catalyst build
(`make catalyst`) is a **deprecated build mode**: it is how the game runs today and keeps
working until the launcher replaces it; `make app`/`run`/`check` are its aliases until then.

## Stage 0 — survey ☑
Done 2026-10-10: imports, where Apple APIs are used, seams, boot sequence, layout, unwinding,
string ABI. Tool: `tools/ida/survey.py`. Results: [research/macho-launcher.md](research/macho-launcher.md).

## Stage 1 — macOS launcher on Apple's loader ☑ (2026-10-10)
Let dyld do the loading so we can focus on the platform layer, the boot and rendering.

Stage 1 is split into three plans: **1a** load the image ☑
([plan](superpowers/plans/2026-10-10-launcher-stage1a-load.md); `make launcher` /
`make launcher-check`: the real game image loads in a plain macOS process with all 18 non-host
libraries stubbed and all static initializers run), **1b** AppPlatform + boot + ANGLE window ☑
([plan](superpowers/plans/2026-10-10-launcher-stage1b-boot.md); `make angle`, `make launcher`,
`make launcher-run`: the engine boots in an AppKit window on ANGLE/Metal and renders the Win10
Edition title screen; `make launcher-check` renders 120 frames), **1c** playable ☑
([plan](superpowers/plans/2026-10-10-launcher-stage1c-play.md): keyboard, raw mouse look,
pointer capture, text entry, sound, no Xbox Live prompt; the owner played a world on
2026-10-10). `make app`/`run`/`check` now build and run the launcher.

1. ☑ **Image preparation** (`tools/` script, run at install time like `convert.sh`): copy the
   binary; `MH_EXECUTE` → `MH_DYLIB` with an `LC_ID_DYLIB`; drop `LC_MAIN`; retag the platform
   to macOS (`LC_BUILD_VERSION`); point each framework's `LC_LOAD_DYLIB` at our stub library;
   ad hoc sign. Test with a small arm64 iOS executable we compile ourselves (no game files).
2. ☑ **Stub libraries**, generated from the import list (Apple API names only, safe to
   commit): functions log `mcfm: stub <lib>:<symbol>` once and return like a message to nil
   (x0, x1, d0–d3 zero); data symbols are 256 zero bytes. Test: every import resolves.
   **Decided 2026-10-10**: stub every framework, including those macOS also has, and libobjc;
   only libSystem, libc++ and libz bind to the host. Stage 1 thereby proves the binary runs
   without Apple frameworks. As built (1a): stubs log `mcfm: stub <lib>:<symbol>` once (and
   `objc_msgSend` once per selector); ObjC classes are plain data stubs because the image's
   ObjC metadata is hidden from the runtime (below).
3. ☑ **GL**: bind the 84 engine `gl*` imports to ANGLE (GLES 3 on Metal). **Decided**: prebuilt
   ANGLE binaries, fetched by a script into a git-ignored folder (pinned version + checksum).
   Shaders ship as GLSL ES in the game data. As built (1b): the `OpenGLES` stub re-exports
   ANGLE's `libGLESv2` (`build_stubs.sh --provider`), so only symbols ANGLE lacks are stubbed.
4. ☑ **Our AppPlatform**: call the base ctor `0x10045F678` on a 360+ byte object, then set the
   vptr to our own vtable: a copy of the base vtable `0x100E649C0` with our overrides (Win10
   edition, UI scaling rules, input mode, pointer show/hide, keyboard, paths, …). This is
   step 4 of the generic AppPlatform plan in [HANDOFF.md §6.1](HANDOFF.md#61-generic-appplatform).
   Every slot gets a FakePlatform host test. As built (1b): `shared/launcher/app_platform.cpp`
   (the 19 pure slots, paths, Win10 policy; pointer and keyboard slots come with input in 1c).
5. ☑ **Boot and frame loop**: `MinecraftClient` ctor `0x10006E2DC`, `AppContext`, `App::init`
   `0x1000555BC`, `setRenderingSize` / `setUISizeAndScale`, then per frame: bind the default
   framebuffer → run main-thread jobs → `update()` → present. Window via AppKit +
   `CAMetalLayer` (ANGLE surface). As built (1b): `shared/launcher/engine.cpp`,
   `macos/launcher/main.mm` (60 Hz timer; `--frames N`, `--screenshot`).
6. ☑ **Seams**: replace HTTP (#1) with our own client or a "no network" stub, store (#2) with
   "no products", Xbox services (#3) and TCUI (#4) with "signed out". Hook on Stage 1's mapped
   image by patching the call sites or the target entry points. As built (1b): **hook table**
   (`mcfm_image.py dylib --hooks` patches each hooked entry to jump through a table at the end of
   `__DATA`; `mcfm-launch` fills it from a dyld add-image callback before any initializer).
   Hooked: #1 the telemetry upload (the engine's only HTTP use) → no-op; #2 `createStores` → one
   null store (licensed, not a trial); #3 Xbox config singleton → zeroed object; the Xbox Live
   first-launch prompt push → no-op. #4 TCUI was never reached in play; hook it if it is.
7. ☑ **Input**: reuse `shared/` keyboard/mouse (it writes the engine's `Keyboard`/`Mouse`
   queues directly) and pointer capture. As built (1c): `LauncherPlatform` (shared) +
   `macos/launcher/input.mm` (NSEvent keys/buttons/wheel, GCMouse raw look, CoreGraphics
   capture, text entry through the engine's `Keyboard` text queue; `MCFM_LOOK_SCALE`).
8. ☑ **Audio**: FMOD's output uses a RemoteIO AudioUnit and `AudioSession*`: our AudioToolbox
   wrappers implement the subset it calls (RemoteIO → CoreAudio default output; session calls
   succeed) — the same wrapper API is reimplemented on AAudio for Android. As built (1c):
   `macos/launcher/audio_toolbox.cpp`, the `AudioToolbox` stub's provider.
9. ☑ **Runtime census**: play a session (menus, world creation, gameplay, chat, settings) and
   collect every `mcfm: stub` line (`build/launcher/census.txt`) → the real shim list for
   Stages 2–3. A full play session calls 2 stubs (`objc_autoreleasePoolPush/Pop`); everything
   else the game needs is provided (ANGLE, the audio provider) or hooked.

Acceptance (met 2026-10-10): `make app GAME=…` (CLI, no UI) builds `dist/launcher` (no Catalyst, no UIKit) that
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

## Decisions
- 2026-10-10: Android targets SDK 28 (sideload only).
- 2026-10-10: Stage 1 stubs every framework and libobjc (host: libSystem, libc++, libz only).
- 2026-10-10 (Stage 1a spike): libobjc is stubbed too and the image's `__objc_*` sections are
  renamed `__xbjc_*`, so the system libobjc never reads the game's ObjC metadata (it crashed in
  `readClass` on the stub superclasses). `__PAGEZERO` becomes a 16 KB no-access `__MCFM_PAD`
  segment instead of being removed, because fixup opcodes address segments by index.
- 2026-10-10: ANGLE from prebuilt binaries, fetched into a git-ignored folder. Pinned to the
  official Electron v41.0.3 macOS arm64 release (later releases link ANGLE into the framework
  and no longer ship `libEGL`/`libGLESv2`); install names rewritten to `@rpath`, re-signed ad hoc.
- 2026-10-10 (Stage 1b): engine code is patched only at conversion time (Apple Silicon kills a
  process that executes modified signed pages); runtime writes go only to the hook table.
- 2026-10-10 (Stage 1b): launcher storage is `~/Library/Application Support/MinecraftPE-mcfm/`,
  separate from the Catalyst build's worlds in `~/Documents/games/com.mojang` (sharing them is a
  later decision). Layout as AppPlatform_iOS builds it from Documents (Stage 1c fix): worlds and
  options in `<root>/games/com.mojang/` (`make_host_info`); older misnamed folders are moved.
- 2026-10-10: CLI only; the IPA goes through `make` targets, no launcher UI for now.
- 2026-10-10: Microsoft account, Xbox Live, TCUI and telemetry are dropped (see Patch policy).
- 2026-10-10: the Mac Catalyst build stays as a deprecated build mode (`make catalyst`,
  `catalyst-run`, `catalyst-check`, with a notice). Stage 1 landed the same day:
  `make app`/`run`/`check` build and run the launcher.
- 2026-10-10 (Stage 1c): camera look uses raw GameController (`GCMouse`) deltas, without the
  system's pointer acceleration (owner request).
