# Android launcher — research (Stage 3)

What it takes to run the iOS Mach-O image on Android arm64 with our loader
([LAUNCHER.md](../LAUNCHER.md) Stage 3). ✅ = verified, ❓ = to verify.

## Toolchain and test device (2026-10-10)
- NDK r27d (27.3.13750724, clang 18.0.4 = LLVM 18), platform-tools r37.0.1, emulator
  16489710 (darwin-aarch64), system images `android-37.0;google_apis_ps16k;arm64-v8a` r07 (the
  target: Android 17, 16 KB pages) and `android-28;default;arm64-v8a` r02 (the minimum);
  fetched without Java by `tools/android/fetch_sdk.sh` (SHA-1 from Google's repository
  index). ✅
- The AVD `mcfm28` (Android 9, arm64, 4 KB pages, SELinux enforcing) boots headless on Apple
  Silicon in about a minute (`tools/android/emulator.sh start`). ✅
- Test programs run from `adb shell` in `/data/local/tmp` (domain `u:r:shell:s0`). ✅

## Memory (spike `mapexec`, 2026-10-10)
From the `shell` domain on the emulator: a file in `/data/local/tmp` mapped `PROT_READ|PROT_EXEC`
runs, also `MAP_FIXED` inside a `PROT_NONE` reservation; anonymous RW memory switched to RX with
`mprotect` runs too. ✅ An app targeting SDK 29+ may not execute its own data files (W^X for
`app_data_file`), while anonymous executable memory stays allowed (JITs use it), so the Android
`LoaderOS` copies each segment into anonymous memory (`pread`) and `mprotect`s it to the
segment's protection; no code signatures. The Mach-O segments are 16 KB aligned, so they fit
4 KB and 16 KB pages. (The app domain is checked in Stage 3c ❓.)

## Our loader on Android (spike `run`, 2026-10-10)
`shared/loader` built unchanged with NDK r27 loaded a small macOS arm64 dylib (classic fixups)
on the emulator: rebases and binds to bionic (`strlen`) correct. ✅ (The spike's initializer
had been evaluated away by clang at compile time; fixtures make an external call in their
initializers so they really run.)

## Darwin arm64 ABI vs Android (AAPCS64)
- **Variadic calls**: Darwin passes every variadic argument on the stack in 8-byte slots; AAPCS64
  passes them in x/v registers first. The game's `snprintf("%d|%.2f|%s")` through bionic printed
  garbage. Fix, plain C, no assembly: the shim is itself variadic; after `va_start` it sets the
  AAPCS64 `va_list` fields `__gr_offs = __vr_offs = 0` (register areas exhausted), so `va_arg`
  reads from `__stack`, which is the caller's stack pointer: the Darwin layout. A Darwin
  `va_list` (`char *`) becomes an AAPCS64 one with `__stack = ap` and both offsets 0. Verified
  for int, double, pointer (`7|3.25|str|14`). ✅
- `long double` is 8 bytes on Darwin arm64 and 16 on Android: none of the 286 libc++ imports
  take one ✅; `printf("%Lf")` from the game would read the wrong width ❓ (not seen yet).
- `char` is signed on Darwin, unsigned on Android arm64: code we compile that must behave like the
  engine's (libc++, shims) is built with `-fsigned-char`.
- `x18` is reserved on both; the image never uses it.

## libc++ (Apple ABI)
- All 285 named libc++ imports of the game are in LLVM 18.1.8's
  `arm64-apple-darwin.libcxxabi.v1.stable.exceptions.nonew.abilist`. ✅ So LLVM 18 libc++ built
  with Apple's settings covers them:
  - namespace `std::__1` (ABI v1), `_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT` (Apple arm64);
  - `_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION = 3` (NonUniqueARMRTTIBit, what Apple arm64
    uses: type_info names may carry a "non-unique" high bit);
  - the threading types inside libc++ objects that game code constructs inline (`std::mutex`
    is a `pthread_mutex_t` initialized with Darwin's `PTHREAD_MUTEX_INITIALIZER`, signature
    `0x32AAABA7`): libc++ must use the Darwin pthread layer, not bionic's, through
    `_LIBCPP_HAS_THREAD_API_EXTERNAL` and our `<__external_threading>`;
  - `std::ctype<char>` is partly inline in the game: its table has Darwin's 32-bit
    `_CTYPE_*` masks (`__DefaultRuneLocale`), so our libc++ uses Darwin's ctype masks and table.
- libunwind: compact unwind support and `__unw_add_find_dynamic_unwind_sections` exist in LLVM
  18 but are compiled only `#ifdef __APPLE__` (`config.h`, `AddressSpace.hpp`
  `findUnwindSections`). A small patch enables both on Android, the dynamic finders consulted
  before `dl_iterate_phdr`.

## The game's libSystem imports (273)
By the work a shim does:
- **Same ABI** (resolved to bionic): string/memory, math, `malloc` family, `qsort`, `rand`,
  `strto*`, `atoi`/`atof`, `time`/`clock`/`gettimeofday`/`difftime`, `struct tm` functions
  (same layout), `getpid`/`getuid`/`geteuid`/`getenv`, `sleep`/`usleep`/`sched_yield`,
  `read`/`write`/`pread`/`close`/`fsync`/`unlink`/`rmdir`/`mkdir`/`rename`/`remove`/`pipe`,
  `FILE *` functions (`FILE` stays opaque: `__stdinp`/`__stderrp` hold bionic's), `inet_*`,
  `__memcpy_chk` family, `__stack_chk_*`, `__cxa_atexit`, `abort`.
- **Variadic / va_list**: `printf`, `fprintf`, `sprintf`, `snprintf`, `asprintf`, `sscanf`,
  `fscanf`, `__snprintf_chk`, `open`, `fcntl`, `ioctl`; `vfprintf`, `vsnprintf`.
- **Different numbers or layouts**: `errno` (`___error`; Darwin numbers), `stat`/`fstat`/
  `lstat`, `opendir`/`readdir`/`readdir_r`, `O_*`/`F_*`/`MAP_*`/`_SC_*` constants, sockets
  (`sa_len`, `AF_INET6` 30 vs 10, `SOL_SOCKET` 0xffff vs 1 and option numbers), `addrinfo`
  (member order), `hostent` functions, `getifaddrs`, `select`/`poll`, signals (`sigaction`
  layout, numbers), `uname`, `tcgetattr`/`tcsetattr`, `sysctlbyname`, locale masks
  (`newlocale`), `strerror`.
- **pthread**: Darwin sizes (mutex 64, cond 48, once 16, attr 64 bytes, `pthread_key_t` 8) and
  static initializer signatures; different constants (`PTHREAD_MUTEX_RECURSIVE`, detach
  state, scheduling policies); `pthread_setname_np(name)` takes one argument on Darwin.
- **Darwin only**: `mach_absolute_time`/`mach_timebase_info`, mach semaphores, `host_*`,
  `dispatch_*` (+ `_dispatch_main_q`), the blocks runtime (`_Block_object_*`,
  `_NSConcreteGlobalBlock`/`StackBlock`), `OSMemoryBarrier`, `memset_pattern16`,
  `__sincos_stret`/`__sincosf_stret`, `__maskrune`/`__tolower`/`__toupper`/
  `__DefaultRuneLocale`, `__assert_rtn`, `kqueue`/`kevent`, `hash_create`/`hash_search`,
  `_dyld_register_func_for_add_image`, CommonCrypto (`CC*`, used by the dropped Xbox code),
  `dyld_stub_binder` (0, as on macOS).
