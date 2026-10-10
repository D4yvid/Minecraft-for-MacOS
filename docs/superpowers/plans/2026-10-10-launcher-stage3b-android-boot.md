# Stage 3b — The Engine Boots and Renders on Android Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the Android emulators, `mcfm-run --boot` loads the game and boots the engine with
our AppPlatform, through the same `shared/launcher` code the macOS launcher uses. It renders
the Win10 Edition title screen into a headless EGL/GLES 3 surface, and FMOD's audio output
runs on AAudio. A screenshot of frame 120 shows the title screen, as on macOS.

**Architecture:** No window yet (that is Stage 3c, our own APK). `mcfm-run` gains a boot mode:
- an EGL pbuffer with a GLES 3 context;
- the launcher's hooks (`shared/launcher/seams.cpp`) installed by the loader;
- the LC_UUID guard;
- `Engine::start` with host directories under `/data/local/tmp/mcfm`;
- N frames, draining the main dispatch queue between frames;
- `suspend()` (the game saves), then a PPM screenshot of the last frame.

Imports resolve as follows:
- **GLES:** the image's `OpenGLES` imports resolve to the system's `libGLESv3.so`. Anything it
  does not export goes through `eglGetProcAddress`, and the OES framebuffer entry points fall
  back to their core names. EAGL's ObjC symbols stay stubbed.
- **Audio:** `libmcfm_audiotoolbox.so` provides the AudioToolbox subset FMOD's iOS output uses:
  the RemoteIO AudioUnit on an AAudio stream (pull callback), and the AudioSession answers. The
  AudioQueue and AudioFile entries report "unimplemented". The game's sound banks are PCM16
  and FMOD ADPCM, decoded by FMOD itself, so they don't need these.

**Tech Stack:** C++11 (`shared/`), C++17 (Android launcher), NDK r27d, EGL/GLES 3, AAudio, adb.

**Spec:** `docs/LAUNCHER.md` Stage 3 item 4 (GL, audio); research
`docs/research/android-launcher.md`. Spike 2026-10-10 on the API 37 emulator, from `adb shell`:
- EGL pbuffer + GLES 3.0 work ("Android Emulator OpenGL ES Translator (Apple M4)", Metal);
- `eglGetProcAddress` returns `glBindVertexArrayOES`, `glDiscardFramebufferEXT` and
  `glBindRenderbufferOES`;
- `glReadPixels` reads back the clear color;
- AAudio float 48 kHz stereo calls back as expected, even with the emulator's `-no-audio`.

## Global Constraints

- Everything from Stage 3a holds:
  - target SDK 37, minimum API 28, 16 KB-aligned binaries;
  - Darwin numbers only from `darwin_abi.h`;
  - no Mojang files committed;
  - `make test` without an SDK, emulator or game files.
- `shared/launcher` stays C++11 and platform-free, unchanged in behaviour for macOS (the macOS
  launcher must still pass `make check`).
- Engine addresses are used only after the LC_UUID check of the mapped image.
- Logs are prefixed `mcfm:`. Device checks need a running emulator; the game-dependent checks
  need `make app`'s converted game and its `data/`.

## Review Focus

1. **GL lookup order:** a GLES function the system library exports (the common case) must come
   from it, never from a stub. A missing one must fail the load loudly, never silently stub a
   draw call (Task 1 test).
2. **Audio callback buffers:** FMOD may set a non-interleaved or 16-bit format. The render
   callback's AudioBufferList must match what the unit was configured with, or audio is noise
   or out of bounds (Task 2 cases for float/int16, interleaved/non-interleaved).
3. **Engine boot on Android threads:** the main queue is drained on the boot thread between
   frames. Work the engine queues from initializers must not deadlock the first frame (Task 3).
4. **Exit:** `suspend()` before exit saves the world/options to the Android paths. A second run
   finds the options (Task 3 checks `options.txt` exists after the first run).
5. **16 KB pages:** the 82 MB `data/` is read through `fopen`/`mmap` like everything else. The
   API 28 run checks 4 KB pages (Task 4).

---

## File Structure

| File | Responsibility |
|---|---|
| `android/launcher/loader_android.cpp` | providers: `mcfm_stub_OpenGLES` → `libGLESv3.so` + `eglGetProcAddress` + OES→core fallback; `mcfm_stub_AudioToolbox` → `libmcfm_audiotoolbox.so` |
| `android/launcher/audio_toolbox_android.cpp` | `libmcfm_audiotoolbox.so`: AudioComponent/AudioUnit (RemoteIO) on AAudio, AudioSession answers, AudioQueue/AudioFile unimplemented |
| `android/launcher/boot.cpp` | `mcfm-run --boot`: EGL pbuffer, `Engine` from `shared/launcher`, frames, screenshot |
| `android/launcher/tests/audio_test.cpp` | drives the AudioUnit API the way FMOD does |
| `tools/tests/gl_fixture.c` + `android_launcher_test.sh` | a Mach-O image calling GLES through the `OpenGLES` framework under `mcfm-run --gl` |
| `tools/android/frames_check.sh` + `make android-frames-check` | pushes `data/` (once), boots the game, pulls the screenshot and checks it |
| `Makefile`, docs | build rules; LAUNCHER.md 3b ☑, research, HANDOFF, CLAUDE.md |

Branch: `claude/android-3b` (from `main`).

---

### Task 1: GLES through the system library

**Interfaces:**
- `AndroidLoaderOS` gets a provider for `mcfm_stub_OpenGLES`:
  - `symbol()` tries `dlsym(libGLESv3, name)`, then `eglGetProcAddress(name)`;
  - for `gl*OES` names of core GLES 2/3 functions it then tries the name without `OES`;
  - only then the stub's `_name` (EAGL classes/constants);
  - an unresolved `gl*` name returns null, so the load fails naming it.
- `mcfm-run --gl` creates a pbuffer EGL context (GLES 3, RGBA8, depth 24, stencil 8,
  1280×720) before loading, so initializers and `--call` can use GL.

- [ ] Test first: `gl_fixture.c` (Mach-O, links `-framework OpenGLES`-style imports through the
  converter like the loader fixture). `int gl_check(int)` calls `glClearColor(0.25, 0.5, 0.75, 1)`,
  `glClear`, `glReadPixels` (1 pixel) and returns the packed RGB.
  `glBindRenderbufferOES`/`glGenVertexArraysOES` are called once. Under `mcfm-run --gl --call
  gl_check` the result is `0x4080bf`, and the run log contains no `mcfm: stub OpenGLES:_gl*`.
  RED (resolves to stubs).
- [ ] Implement; GREEN on the API 37 emulator.
- [ ] Commit: "Android: GLES imports resolve to the system's GLES (Stage 3b)".

### Task 2: AudioToolbox on AAudio

**Interfaces** (`libmcfm_audiotoolbox.so`, exported with the iOS names the stub provider maps):
- `AudioComponentFindNext` (only output/RemoteIO), `AudioComponentInstanceNew/Dispose`,
  `AudioUnitSetProperty/GetProperty`, `AudioUnitInitialize/Uninitialize`,
  `AudioOutputUnitStart/Stop`, `AudioUnitRender` (unimplemented).
- Properties:
  - StreamFormat (8) holds the AudioStreamBasicDescription, read back on Get;
  - SetRenderCallback (23);
  - MaximumFramesPerSlice (14);
  - EnableIO (2003): output only.
- `AudioSessionGetProperty`: the macOS answers (2 channels, 48 kHz, ambient) plus the current
  route. `AudioQueue*` and `AudioFile*` return `kAudio_UnimplementedError` (-4), logged once.
- Start opens an AAudio stream in the unit's format: float or int16, interleaved, the rate and
  channel count. The data callback calls the render callback with an AudioBufferList on
  AAudio's buffer. For non-interleaved formats it uses per-channel scratch buffers, interleaved
  after.

- [ ] Test first: `audio_test.cpp` (NDK, links the provider directly) configures a unit the way
  FMOD's iOS output does: RemoteIO, then 48 kHz interleaved float stereo, then int16
  non-interleaved. Each case starts the unit, collects ≥ 1 s of callbacks with the expected
  `mNumberBuffers`/`mDataByteSize`, stops it, and checks that guard bytes around the buffers
  are intact.
- [ ] Implement; GREEN on the emulator.
- [ ] Commit: "Android: AudioToolbox output unit on AAudio".

### Task 3: Boot the engine

**Interfaces:**
- `mcfm-run <image> --boot --data <dir> --home <dir> [--frames N] [--screenshot out.ppm]`:
  - the UUID guard (`shared/apple/macho_uuid.cpp`, built for Android);
  - hooks from `shared/launcher/seams.cpp`;
  - EGL as in Task 1;
  - `HostInfo` via `make_host_info(home, data, home + "/tmp")`;
  - `Engine::start(EngineAddresses::for_slide(slide), …)`;
  - per frame: `frame()`, `mcfm_darwin_drain_main_queue()`, `eglSwapBuffers`;
  - at the end `suspend()`, the screenshot (`macos/launcher/screenshot.h` is moved to
    `shared/launcher/screenshot.h` if platform-free), exit 0.
  - Prints `mcfm: engine started (1280x720)` and `mcfm: N frames rendered`, like macOS.
- `tools/android/frames_check.sh`:
  - pushes `data/` once (a marker file holds its checksum);
  - runs 120 frames;
  - pulls the screenshot and converts it with `sips` for viewing.

  The checks:
  - the image is not uniform (≥ 1000 distinct colors);
  - the top-centre region has the logo's light pixels, as the macOS screenshot of the same
    frame shows;
  - `games/com.mojang/minecraftpe/options.txt` exists on the device after the run;
  - stub census lines are printed.

- [ ] Test first: `frames_check.sh` fails (no `--boot`).
- [ ] Implement `boot.cpp`; build `shared/launcher` and `shared/src` sources for Android
  against the runtime.
  - Every crash or missing piece found while booting gets a fixture or unit test first.
- [ ] GREEN on API 37: look at the screenshot.
- [ ] Commit: "Android: the engine boots and renders the title screen".

### Task 4: Both API levels, docs

- [ ] `make android-test`, `make android-boot-check` and `make android-frames-check` on API 28
  too.
- [ ] Docs:
  - LAUNCHER.md: 3b ☑ with the numbers;
  - research: the renderer string, audio formats FMOD chose, census;
  - HANDOFF/CLAUDE.md: the new command.
- [ ] Commit: "Stage 3b: the engine boots and renders on Android".

## Acceptance

- `make android-frames-check` renders the Win10 Edition title screen (screenshot checked) on
  the API 37 and API 28 emulators, with FMOD's AAudio stream running and the options saved.
- `make test` is green, and the macOS launcher still passes `make check`.
