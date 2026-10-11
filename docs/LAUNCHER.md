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
   capture, text entry through the engine's `Keyboard` text queue; `MCFM_LOOK_SCALE`). Custom
   skins (`pickImage`): an open panel for images, re-encoded as PNG (`image_pick.mm`).
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

## Stage 2 — our own loader, still on macOS ☑ (2026-10-10)
Replace dyld so the same loader can later run on Android/Linux, debugged where tools are best.
Plan: [superpowers/plans/2026-10-10-loader-stage2.md](superpowers/plans/2026-10-10-loader-stage2.md).
`make run`/`check` use it by default; `LOADER=dyld` (`mcfm-launch --loader dyld`) falls back to Apple's.

1. ☑ Parse load commands (`shared/loader/macho_file`); reserve the image's range, map each
   segment, apply the `LC_DYLD_INFO_ONLY` rebase/bind/lazy-bind/weak-bind opcodes decoded by our
   own decoder (`shared/loader/fixups`). On macOS the segments are mapped from the signed file
   after registering its code signature (`F_ADDFILESIGS_RETURN`, as dyld does); Android will copy
   into anonymous memory and `mprotect` (Stage 3).
2. ☑ Imports through `LoaderOS` (macOS: `dlopen`/`dlsym` of the host libraries, stubs and
   providers); weak binds coalesce host-first like dyld (`operator new/delete` → libc++), then
   fall back to the image's own export. Ordinal -1 (flat) searches every loaded library; a
   missing weak-linked library binds its symbols to 0. Refused with a message: thread-local
   variables, self-binds to re-exports/absolute/resolver symbols, initializers outside
   `__TEXT`, an unsigned image where signing is required, or a signature that does not cover
   the mapped segments.
3. ☑ The 3,972 static initializers run in order (`__mod_init_func`; `__init_offsets` also
   supported); ObjC metadata gets no runtime.
4. ☑ **Exceptions**: `__unw_add_find_dynamic_unwind_sections` points libunwind at the image's
   `__unwind_info`/`__eh_frame` (fixture test: thrown 5 frames deep, caught inside the image,
   also from an initializer). Needs macOS 14+; on older systems the load fails with a message.
5. ☑ The LC_UUID guard runs on the file before mapping and again on the mapped header before any
   fixup; the loader returns the slide, verifies and fills the hook table itself (errors name
   the hook), and passes argc/argv/envp to initializers.

Acceptance (met 2026-10-10): `make loader-check` loads the game with dyld and with our loader in
one process and finds every one of its 95,676 fixup locations equal (3 `strcmp`/`strncmp`
entry-point variants and `dyld_stub_binder` accepted explicitly); the owner played a world with
our loader; fixture tests cover fixups, imports, initializers, exceptions and hooks.

## Stage 3 — Android ☑ (2026-10-10)
1. ☑ **Target the latest Android: SDK 37 (Android 17), 16 KB pages**; minimum API 28 (the
   oldest image we test). Prebuilt APKs, not on Google Play (its policy forbids running code
   that did not come from Play, i.e. the user's IPA). From target SDK 29 an app may not map its
   own files executable, so the loader copies each segment into anonymous memory and
   `mprotect`s it (verified on the API 28 emulator); our `.so` files are 16 KB aligned.
   Split: **3a** load the game and run its initializers on Android (loader, libc translation,
   Apple-ABI libc++, stubs; command-line test over adb); **3b** engine boot, EGL/GLES, audio;
   **3c** our APK (Kotlin, built by make) with the game bundled, window, input, text.
2. ☑ **libc translation layer** (Darwin ABI → bionic), `android/launcher/darwin/`: every
   libSystem symbol the game imports is in one sorted table (`symbols.cpp`: shimmed, bionic's,
   or the runtime's); numbers and layouts come from `darwin_abi.h`, generated from the macOS
   SDK. A conformance fixture (`tools/tests/darwin_conformance.cpp`, Darwin code built on the
   Mac) prints the same transcript on the Mac and on Android. Covered:
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
3. ☑ **libc++** (`libmcfm_runtime.so`, `android/launcher/runtime/`): LLVM 18.1.8 libc++,
   libc++abi and libunwind with Apple's arm64 ABI (`std::__1`, alternate string layout,
   NonUniqueARMRTTIBit type_info, Darwin `mbstate_t` and ctype tables, threading over the Darwin
   pthread layer) and libunwind patched for compact unwind on Android; exports all 285 libc++
   symbols the game imports.
4. ☑ GL: the image's `OpenGLES` imports resolve to the system's GLES 3 (core functions for the
   OES/EXT names GLES 3 has; never `eglGetProcAddress`, which can hand back GLES 1 entry points;
   EAGL stays stubbed). Audio: FMOD's
   RemoteIO AudioUnit on an AAudio stream (`android/launcher/audio_toolbox.cpp`); AudioQueue and
   AudioFile unimplemented (the banks are PCM16 and FMOD ADPCM). The engine boots with the
   macOS launcher's AppPlatform code (`shared/launcher`) in `mcfm-run --boot` (headless GLES 3
   pbuffer; 3c draws into the app's window).
5. ☑ Android platform layer: **our own APK** (`android/app/`, Kotlin; built from scratch by
   `make android-app IPA=…` with aapt2, kotlinc, d8, zipalign and apksigner, no Gradle, nothing
   from Mojang's APK): a launcher for the game bundled in it. The build puts the IPA's
   `minecraftpe2` and `data/` in `assets/game/` (so the APK is a local build, never committed);
   on first start (and after an update that brings another game) the app copies them out and
   converts the binary on the device (`shared/loader/convert.cpp`, byte-identical to
   `mcfm_image.py`). `GameActivity` hands its surface, lifecycle and input to a native render
   thread (`android/launcher/game_thread.cpp`) that owns EGL and the engine. Touch goes to
   `Multitouch::feed`; keys, a mouse and soft-keyboard text through `shared/` keyboard_mouse;
   custom skins through the system photo picker (`pickImage`);
   the game saves when the app goes to the background. Worlds live in internal storage
   (`files/home`); an update that changes the launcher's hooks converts the kept binary again.
   `make android-app` builds the release APK (`mcfm.apk`), `make android-app-debug` the
   debuggable one the checks use. It replaces the old Win10-UI mod that patches Mojang's APK (`android/`,
   `make android-apk`, legacy).

Stage 3a acceptance (met 2026-10-10): `make android-boot-check` loads the converted game with
`mcfm-run` and all 3,972 initializers run, on Android 17 (16 KB pages) and Android 9 (4 KB);
`make android-test` (pthread, runtime, files, network unit tests; loader fixture; Darwin
conformance transcript) is green on both.

Stage 3b acceptance (met 2026-10-10): `make android-frames-check` boots the game on Android 17
(16 KB pages) and Android 9, renders the Win10 Edition title screen (120 frames, screenshot
checked), FMOD plays through AAudio (24 kHz int16 stereo) and the options are saved on suspend.

Stage 3c acceptance (met 2026-10-10): `make android-app` builds a signed, 16 KB-aligned APK
(target SDK 37, min SDK 28, about 65 MB with the game); `make android-app-check` passes on
Android 17 (16 KB pages) and Android 9 from a clean install: the first start prepares the bundled
game, the title screen renders in the window, touch taps create a world and it renders, the game saves in the
background and draws again after resuming (twice, then screen off and on; screenshots checked),
a stale image is converted again, and a damaged copy shows its message without crashing and is
prepared again on the next start. Text input, suggestions, back and a hardware keyboard were checked by
hand on both.

Acceptance: an APK (target SDK 37) that installs on Android 9+ arm64, including 16 KB-page
devices, imports a user-supplied IPA and plays — met by 3c.

## Later
- Linux arm64 / Windows on ARM launchers (Stage 2 loader + Stage 3 translation, different host).
- Our own renderer behind the `gl*` imports ([research/renderer.md](research/renderer.md)).
- Module system on top of the launcher's platform layer (HANDOFF §6.3).

## Decisions
- 2026-10-10: the Android app is our own APK that launches the launcher `.so` (not a patched
  Mojang APK); the old Android mod's library is named `libmcfm.so` like everything else.
- 2026-10-10: the APK always bundles the game (no import screen): the app is a launcher for it.
- 2026-10-10: the Android app code (activity, game preparation, settings) is written in Kotlin; the
  launcher, loader and Darwin layer stay native (C++).
- 2026-10-10: Android targets the latest SDK (37, 16 KB pages), minimum API 28; prebuilt APKs
  outside Google Play (replaces the earlier SDK 28 decision). Toolchain: NDK r27d (LLVM 18),
  libc++/libc++abi/libunwind from LLVM 18.1.8 built with Apple's arm64 ABI settings.
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
- 2026-10-10 (Stage 2): our own loader is the default (`LOADER=dyld` keeps Apple's available);
  its fixups come from our opcode decoder, never from `dyld_info` text (which prints weak binds
  with stale symbol names).
