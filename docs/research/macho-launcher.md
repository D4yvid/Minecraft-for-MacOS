# Mach-O launcher — Stage 0 survey (Minecraft PE 0.15.10, iOS arm64)

Status: **research notes** for [LAUNCHER.md](../LAUNCHER.md). Facts are marked ✅ verified (with
how) or ❓ assumed / to verify. Addresses are unslid (image base `0x100000000`).

Reproduce: `tools/ida/survey.py <db> out.json` (imports, users, seams, initializers — ~6 s),
`otool -l`, `dyld_info -fixups`, `objdump --unwind-info` on `game-files/ios/minecraftpe2-arm64`.

## Verdict

The engine is already platform-free at the binary level. ✅ None of the 45,184 functions below
`0x100700000` (the engine) touch an Objective-C, UIKit, Foundation, CoreFoundation or other
Apple framework import (`survey.py`, `engine` lists empty for all Apple-only libraries). Everything
Apple-specific lives above that address, in the iOS glue and in third-party libraries (Microsoft
account, Xbox Live, FMOD). The engine reaches that code through the `AppPlatform` vtable and
**9 direct call seams** (below). A launcher that never runs the iOS glue and replaces those
seams needs only libc, libc++, OpenGL ES and an audio backend.

## Binary layout ✅ (`otool -l`, `otool -hv`, `dyld_info -fixups`)

| Item | Value |
|---|---|
| Header | `MH_EXECUTE`, arm64 (`CPU_SUBTYPE_ALL`, not arm64e → no pointer authentication), `PIE`, `WEAK_DEFINES`, `BINDS_TO_WEAK` |
| Load commands | `LC_MAIN`, `LC_VERSION_MIN_IPHONEOS`, `LC_ENCRYPTION_INFO_64` (cryptid 0), `LC_DYLD_INFO_ONLY` (classic opcode fixups, no chained fixups) |
| `__TEXT` | `0x100000000`, 0xE34000 bytes, r-x (`__text` 12.4 MB) |
| `__DATA` | `0x100E34000`, 0x1C8000 bytes, rw- (`__common` + `__bss` 0xCE9E8 zero-fill) |
| `__LINKEDIT` | `0x100FFC000`, 0xE4000 bytes |
| Fixups | 91,698 rebases, 4,308 binds, 695 lazy binds |
| Static initializers | 3,972 (`__mod_init_func` 0x7C20 bytes) |
| Objective-C | 107 classes, 9 categories, 1 non-lazy class (`+load`; probably `__ARCLite__`, the ARC back-deployment shim ❓) |
| Thread-local storage | none (no `__thread_*` sections, no `__tlv_bootstrap` import) |
| Segment alignment | 16 KB (iOS page size); also valid on 4 KB-page systems |

### Exceptions and unwinding ✅ (`objdump --unwind-info`)
- `__unwind_info` (compact unwind) describes ~34,100 functions: 13,690 frame-based, 7,887
  frameless, 12,366 with an LSDA (C++ cleanups/catch), 188 without unwind info, and **only 3 in
  DWARF mode** — `__eh_frame` is 0xCC bytes. `__gcc_except_tab` (LSDAs) is 1.2 MB.
- Two personality routines (`__gxx_personality_v0` and, for 18 functions, presumably
  `__objc_personality_v0` ❓).
- 11,827 functions call `_Unwind_Resume`, 7,627 of them in the engine.
- Consequence: outside Apple's own loader, exceptions only work if our unwinder reads compact
  unwind info for our mapped image (LLVM libunwind has the parser; it must be enabled and given
  the sections).

### `std::string` layout ✅ (IDA decompilation of `AppPlatform_iOS` ctor)
Long/short flag in the high bit of byte 23 (`*(char *)(s + 23) < 0` → long): libc++'s
**alternate string layout** (`_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`, the Apple-ARM default),
ABI v1 (`std::__1`). Any libc++ we pair with this binary on other systems must use this layout.

## Imports ✅ (`nm -um`, `survey.py`)

| Library | Imports | Used outside iOS glue | Notes |
|---|---:|---:|---|
| libc++ | 285 | 230 | ABI v1, alternate string layout |
| libSystem | 272 | 249 | libc, pthreads, sockets, math, dispatch, mach, CommonCrypto, blocks |
| OpenGLES | 95 | 84 | the 11 glue-only ones: EAGL context/layer constants, `*OES` renderbuffer calls, `glDiscardFramebufferEXT`, `glGetRenderbufferParameteriv`, `glCompressedTexImage2D` |
| libobjc | 62 | 40 | |
| UIKit / Foundation / CoreFoundation | 35 / 35 / 35 | 5 / 15 / 18 | ObjC message sends are not imports, so these undercount ObjC use |
| Security, AudioToolbox, CoreGraphics, libz, CFNetwork, XSAPITCUI, SystemConfiguration, StoreKit, GameController, WebKit, AVFoundation, QuartzCore | 27, 27, 10, 10, 10, 8, 6, 4, 4, 2, 2, 1 | | `XSAPITCUI` is Microsoft's Xbox UI framework, shipped inside the app |

Darwin-specific libc used **by the engine itself** (everything else Darwin-only is used by
third-party code):
- `__DefaultRuneLocale` (12 engine functions; inlined `<ctype.h>` macros read Darwin's rune table),
- `__sincosf_stret` / `__sincos_stret` (68; Darwin ABI helpers returning sin+cos in registers),
- `memset_pattern16` (5), `_Unwind_Resume` (7,627).

Used only above `0x100700000`: `dispatch_*`, `mach_*`, `semaphore_*`, `kqueue`/`kevent`,
`OSMemoryBarrier`, the blocks runtime, CommonCrypto (`CC*`), `getifaddrs`, `sysctlbyname`,
`host_statistics`, `_dyld_register_func_for_add_image`.

## Where the Apple-dependent code is ✅ (`survey.py` + strings per 64 KB range)

| Range | What | Evidence |
|---|---|---|
| `0x100000000–0x1006FFFFF` | **Engine** (45,184 functions) | no Apple framework imports used |
| `0x100700000–0x10071FFFF` | iOS glue: `AppPlatform_iOS` (ctor `0x100704E84`), `minecraftpeViewController`, `EAGLView`, `IOSHttpRequestGlue`, `ShowKeyboardView`, `GameControllerHandler_iOS`, `StoreManager` (StoreKit) | ObjC class names, StoreKit selectors |
| `0x100730000–0x10077FFFF` | Microsoft account library (`MSA*` classes: SOAP/STS auth, keychain, web views) | `login.live.com`, `MSA*` |
| `0x100780000–0x1007DFFFF` | Xbox Live sign-in glue (`XBL*`, `XboxProvider`, `XBLiOSGlobalState`) and the Xbox services iOS layer | `XBLSignInState`, `getRPSTicketWithScope:` |
| `0x100810000–0x1008BFFFF` | Xbox services (xsapi) and TCUI (friend finder, profile card, telemetry, gamerpics) | `displayFriendFinderWithCompletionBlock:`, `sharedTelemetryManager` |
| `0x100970000–0x10097FFFF` | TLS certificate checks (`SecCertificate*`, `SecPolicy*`) | Security imports |
| `0x100AD0000–0x100B1FFFF` | **FMOD** low-level audio: RemoteIO AudioUnit output, AudioQueue/AudioFile decoding | `FMOD mixer thread`, FMOD build paths |
| `0x100BD0000–0x100BEFFFF` | Telemetry HTTP (`XLSCll`, `vortex.data.microsoft.com`) and `__ARCLite__` | `X-AuthXToken`, ObjC runtime names |

1,245 ObjC methods + 103 `AppPlatform_iOS` slots are the glue roots; 1,678 functions are only
reachable from them. Non-engine code with no Apple use (libraries such as zlib/JSON/LevelDB/
RakNet ❓) is also above `0x100700000`; the engine calls into it heavily, which is fine.

## Seams: engine → Apple-dependent code ✅ (`survey.py` "seams", direct calls, ≤ 4 levels)

| # | Engine caller(s) | Target | What |
|---|---|---|---|
| 1 | `sub_1003B42A8` | `sub_10070D78C` → `sub_10070D530`; `sub_10070D790` → `sub_10070D2E4` | HTTP request backend: constructs an `IOSHttpRequestGlue` (NSURLConnection) and sends it |
| 2 | `sub_1004710D0` | `sub_100711774` → `sub_100714F8C` | Store (in-app purchases) factory → `StoreManager` |
| 3 | `sub_100072C9C`, `sub_1003BA078` | `sub_100798B34` | Xbox services singleton (its HTTP client reads `CFNetworkCopySystemProxySettings`) |
| 4 | `sub_10039D840`, `sub_100155E28`, `sub_100149AD8`, `sub_100219C48`, `sub_100147DB4` | `sub_1008955B0`, `sub_100895924`, `sub_100895B80`, `sub_1008962C0`, `sub_100896978` | Xbox TCUI: show friend finder / profile card, telemetry events |

Not found by this method (indirect calls): the `AppPlatform` vtable (319 references to the
singleton), FMOD's output plugin (reached through FMOD's own function tables), and any other
vtable or function-pointer path ❓. The runtime census in Stage 1 (every stub logs its first
call) closes this gap.

## Boot sequence on iOS ✅ (IDA decompilation)

`start` (`0x10070D7CC`) only calls `UIApplicationMain`. The work happens in the view controller:

1. `-[minecraftpeAppDelegate application:didFinishLaunchingWithOptions:]`: TCUI launch delegate,
   `AVAudioSession` (ambient), root view controller, `GameControllerHandler_iOS`.
2. `-[minecraftpeViewController awakeFromNib]` (`0x10070EDDC`):
   - `EAGLContext` (ES 3, falling back to ES 2), multithreaded GL if available, `[view setFramebuffer]`;
   - `AppPlatform_iOS`: `operator new(0x210)`, ctor `0x100704E84(platform, viewController)`.
     The ctor first calls the **base `AppPlatform` ctor `0x10045F678`**, which sets the base
     vtable `0x100E649C0`, zeroes the base fields (base object is **360 bytes**) and stores
     `this` in the singleton `0x100F5E850`. The iOS part then fills locale, documents/temp/
     internal/userdata paths (`+408/+432/+456/+480`), a keychain device ID, `hw.machine`, …;
   - `AppContext`: `operator new(1)` — an empty object;
   - `0x1000556BC(platform + 320, 0)` — a small state object in the base platform ❓;
   - `XBLiOSGlobalState setLaunchViewController:`; remote notification registration;
   - `MinecraftClient`: `operator new(0x428)`, ctor **`0x10006E2DC(app, 0, 0)`**;
   - `initView`, store `width`/`height` into the platform, touch map.
3. `-[minecraftpeViewController initView]` (`0x10070F850`): first time **`App::init`
   `0x1000555BC(app, context)`** (stores the context at `+24`, calls vtable `+192`, sets
   `+16` = initialised, then `0x100055878`); later times `0x1000555A8` (re-attach context). Then
   App slot 21 `setRenderingSize(w, h)`, slot 20 `setUISizeAndScale(w, h, 0)`, and writes the
   view's `+16` int to `(0x100033BC8() + 488)` ❓.
4. `-[minecraftpeViewController drawFrame]` (`0x10070F670`, CADisplayLink): `[view
   setFramebuffer]` (bind FBO, `glViewport`), drain `mMainQueue` (`std::function` jobs posted to the
   main thread), App slot 19 `update()`.
5. `EAGLView`: colour renderbuffer from the `CAEAGLLayer` + 24/8 depth-stencil renderbuffer;
   `presentFramebuffer` = `glDiscardFramebufferEXT` + `presentRenderbuffer:`; `layoutSubviews`
   deletes the framebuffer (recreated on the next `setFramebuffer`).

A launcher reproduces 2–4 in C++: base `AppPlatform` ctor + our vtable, `AppContext`,
`MinecraftClient` ctor, `App::init`, sizes, and a frame loop of "bind default framebuffer → run
main-thread jobs → `update()` → present".

## Key addresses (new) ✅

| Address | What |
|---|---|
| `0x10045F678` | `AppPlatform::AppPlatform()` (base ctor, sets singleton) |
| `0x100E649C0` | base `AppPlatform` vtable |
| `0x100704E84` | `AppPlatform_iOS` ctor (thunk `0x1007058D0`); object size 0x210 |
| `0x10006E2DC` | `MinecraftClient` ctor `(this, argc, argv)`; object size 0x428 |
| `0x1000555BC` | `App::init(AppContext&)` |
| `0x1000555A8` | re-attach `AppContext` on later `initView`s |
| `0x10070D7CC` | `start` |
| `0x10070EDDC`, `0x10070F850`, `0x10070F670` | `awakeFromNib`, `initView`, `drawFrame` |

## Open questions
- ❓ Names of App vtable slot 24 (`+192`, called by `App::init`) and of `0x100055878`
  (cross-check with Android `App::init`).
- ❓ What `0x100033BC8()+488` and `platform+320` are.
- ❓ Which threads the engine calls GL from, and whether `mMainQueue` jobs come only from
  `AppPlatform_iOS`.
- ❓ What FMOD decodes with AudioQueue/AudioFile (music formats) — decides what the audio
  backend must provide.
- ❓ Which of the 3,972 static initializers touch the glue (the survey shows only
  `objc_autoreleasePoolPush/Pop` in about 19 of them).

## Stage 1b findings ✅ (launcher boot, `make launcher-run`)

- **Boot works with the base `AppPlatform` + our vtable.** During boot the engine calls slots
  36, 35, 37 (in the `MinecraftClient` ctor), then 29, 53, 54, 75, 20, 2, 3, 69, 92, 100, 80, 81,
  34, 4, 33, 5 (`App::init`). Only the 19 pure slots need implementing to get this far
  (`shared/launcher/app_platform.cpp`). ✅ (spike with a logging thunk per slot)
- `getSystemRegion` (20) must be `language_REGION` (`en_US`): `MinecraftClient::init` looks it up
  in the loaded language list and dereferences the result. ✅ (crash with `US`)
- The base `getAssetFileFullPath` (53) returns the relative path unchanged; the iOS override
  prefixes `<bundle>/data/`, and the base `readAssetFile` (54) then `fopen`s it. ✅ (IDA)
- The graphics getters (21–24) are engine functions around `glGetString`; our vtable points at
  them directly (`0x10003A850`, `0x10003A8AC`, `0x10003A680`, `0x10003A908`). ✅ (IDA)
- **Seams hit while booting**, each now hooked (`shared/launcher/seams.cpp`):
  - #3 Xbox services config singleton `0x100798B34` (reads `xboxservices.config` through
    NSBundle) → a zeroed object; `MinecraftClient::init` only stores strings into it.
  - #2 `StoreFactory::createStores` (iOS) `0x100711774` → one null store; the `Store`
    interface has 16 slots (order from Android `_ZTV12AndroidStore`), `isTrial` false,
    `isGameLicensed` true; the store calls `StoreListener` slot 2 (`onStoreInitialized`).
  - #1 the engine's only use of the iOS HTTP glue is the telemetry event-batch upload
    `0x1003B42A8` (REST thread) → no-op. The glue's entry points `0x10070D78C/790` are 4-byte
    thunks (too short to hook).
- **Hooks are applied at conversion**: Apple Silicon kills a process executing a modified
  signed page, so `mcfm_image.py dylib --hooks` rewrites each hooked entry to `adrp x16 / ldr
  x16 / br x16` through a table at the 16-byte aligned end of `__DATA`'s last section (the game
  has 0x2450 bytes there, room for 1,162 hooks); the launcher fills the table from a
  `_dyld_register_func_for_add_image` callback, which dyld runs before the image's initializers.
- With ANGLE (Electron v41.0.3, Metal backend, EGL 1.5) the engine compiles its shaders and
  renders the Win10 Edition title screen with the Xbox Live first-launch prompt on top. Booting
  calls only 5 stubs: `objc_autoreleasePoolPush/Pop` and FMOD's `AudioSessionGetProperty`,
  `AudioComponentFindNext`, `AudioOutputUnitStop` (FMOD then runs with no output).

## Stage 1c findings ✅ (input, text, sound, Xbox prompt)

- **Text entry** follows iOS's `ShowKeyboardView`: the engine's `Keyboard` text queue
  (`0x100F5A010`, `std::vector<{std::string; bool}>`, 32-byte elements) gets one element per
  typed character, `"\b"` per deleted character and `{"\n", true}` for return; AppPlatform slots
  9 `showKeyboard` / 10 `hideKeyboard` open and close text boxes (base implementations keep the
  `isKeyboardVisible` flag at `+9`). ✅ (IDA: `-[ShowKeyboardView textDidChange:]`)
- **Sound**: the 511 FSB5 banks are FADPCM (478) and PCM16 (33), decoded by FMOD itself. FMOD's
  iOS output (`0x100AD34EC`) asks `AudioSessionGetProperty('choc')`, then opens RemoteIO
  (`'auou'/'rioc'/'appl'`), sets the stream format (property 8) and render callback (23),
  initializes and starts it; the record path (`0x100AD3B44`) only runs for the play-and-record
  session category. On macOS RemoteIO maps to the default output unit. ✅ (IDA, lldb: FMOD reaches
  `AudioOutputUnitStart` on a real component)
- **Xbox Live first-launch prompt**: `data/ui/xbl_first_launch.json`; the screen is created and
  pushed only by `0x100158624`, which `MinecraftClient::init` calls after pushing the start screen
  (`0x100154730`) while two Options flags (`0x1002B42D4`, `0x1002B42E4`) are unset. Hooked to a
  no-op. ✅ (screenshot: start screen without the prompt)

## Stage 2 findings ✅ (our own loader)

- **Executable mapping on Apple Silicon**: mapping the converted image's segments from the file
  (`mmap MAP_FIXED|MAP_PRIVATE`) after registering its ad hoc code signature with
  `fcntl(F_ADDFILESIGS_RETURN)` runs its code, as dyld does. ✅ (spike, then `make check`)
- **Exceptions**: libunwind's `__unw_add_find_dynamic_unwind_sections` (exported by
  `/usr/lib/system/libunwind.dylib`) serves the image's compact unwind for addresses in its
  `__TEXT`; C++ exceptions thrown and caught inside the image work. ✅ (fixture test)
- **`dyld_info -fixups` is wrong for weak binds**: it prints them with stale symbol names carried
  over from the previous stream (fixture slot `0x100004058`: `kFakeKitValue + 0x8` instead of
  `__ZTISt12length_error`). On the game that rebound lazy-pointer slots to unrelated symbols in
  the spike. Our decoder reads the weak stream itself. ✅
- **Weak coalescing**: the game's weak definitions (`operator new/delete`, typeinfo) bind to
  libc++'s first, as under dyld. ✅ (`make loader-check`: 0 differences)
- **`strcmp`/`strncmp`**: `libsystem_platform` exports a plain implementation (what `dlsym`
  returns) and a dispatching entry (what dyld binds); both are the same function. ✅
- **`dyld_stub_binder`** is no longer exported by macOS's libSystem; with every lazy pointer bound
  at load, nothing calls it (bound to 0). ✅
- `make loader-check`: 95,676 fixup locations (91,698 rebases, 5,003 binds), 0 differences. ✅
