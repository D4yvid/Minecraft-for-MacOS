# Stage 3a — The Game Loads on Android Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On an Android 17 (API 37, 16 KB pages) arm64 emulator, a command-line runner `mcfm-run`
loads the converted iOS game image with our loader and runs all of its static initializers. The
image's imports resolve to three things: a Darwin-to-bionic libSystem layer, libc++/libc++abi/
libunwind built with Apple's arm64 ABI, and generated framework stubs. A conformance fixture
(a Mach-O built on the Mac) prints the same transcript under our loader on macOS and on Android.

**Architecture:** `shared/loader/` is reused unchanged. Everything Android-specific lives in
`android/launcher/`:
- `loader_android.cpp` is `AndroidLoaderOS`. It reserves the image range, copies each segment
  into anonymous memory with `pread`, then `mprotect`s it; target SDK ≥ 29 forbids executing
  app files.
- `darwin/` is the libSystem layer. It exposes a sorted name→address table
  (`mcfm_darwin_symbol`), with per-area files (stdio/errno, files, pthread, mach and dispatch,
  network, locale and misc).
- `libmcfm_runtime.so` links that layer with libc++ 18.1.8, libc++abi and a patched libunwind.
  They are built from the LLVM sources with Apple's ABI settings: `std::__1`, the alternate
  string layout, NonUniqueARMRTTIBit type_info, external threading over the Darwin pthread
  layer, Darwin ctype masks, `-fsigned-char`, and compact unwind with the dynamic section
  finder.
- Framework stubs are the generator's existing C output, compiled by the NDK into
  `mcfm_stub_<lib>.so`.
- Darwin constants and struct layouts come from a generator compiled against the macOS SDK,
  never typed by hand: it writes `darwin/darwin_abi.h`, which is committed and checked by
  `make test`.

**Tech Stack:** C++11 (shared, android launcher), C (shims where variadic), NDK r27d clang 18
(`aarch64-linux-android28`, `-Wl,-z,max-page-size=16384`), LLVM 18.1.8 runtimes, bash, make,
adb + emulator.

**Spec:** `docs/LAUNCHER.md` Stage 3 (item 1, and items 2–3 for 3a);
`docs/research/android-launcher.md` (spike results 2026-10-10):
- file-backed and anonymous RX mappings work;
- `shared/loader` runs unchanged on Android;
- the variadic bridge works in plain C;
- all 285 libc++ imports are in LLVM 18's Apple ABI list;
- libunwind compact unwind is `__APPLE__`-only and needs a patch.

## Global Constraints

- Target SDK 37, minimum API 28 (NDK target `aarch64-linux-android28`). Every `.so` and
  executable is linked with `-Wl,-z,max-page-size=16384`, and the loader works with 4 KB and
  16 KB pages. No Google Play distribution.
- Never commit Mojang files. Downloads only through the pinned, checksummed scripts
  (`tools/android/fetch_sdk.sh`, `fetch_llvm_runtimes.sh`) into git-ignored folders.
- `make test` needs no game files, no SDK and no emulator. Android-only checks skip with a
  message when the NDK is absent. `make android-test` needs a running emulator, and
  `make android-boot-check` also needs `GAME`.
- `shared/loader/` stays C++11 and OS-free, with no Android `#ifdef`s.
- Darwin numbers and layouts come only from the generated `darwin_abi.h`; bionic's come from
  the NDK headers.
- Darwin variadic calls: the shim is variadic and marks its AAPCS64 `va_list` register areas
  exhausted (`__gr_offs = __vr_offs = 0`). A Darwin `va_list` (`char *`) becomes `{__stack = ap}`.
- Code that shares objects with the engine (libc++, shims) is compiled with `-fsigned-char`.
- Logs are prefixed `mcfm:` (on Android: stderr in `mcfm-run`; logcat comes in 3c).
- Unknown imports fail the load naming library and symbol: every libSystem import is listed
  explicitly, either passed through to bionic or shimmed.

## Review Focus

1. **Static pthread initializers.** A Darwin `PTHREAD_MUTEX_INITIALIZER` / `PTHREAD_COND_INITIALIZER` /
   `PTHREAD_ONCE_INIT` object first used from two threads at once must be converted exactly
   once, with no lost wakeups (Task 2 test with 8 threads racing on a static mutex).
2. **Exceptions across both unwinders' frames.** A throw from game code through a bionic
   callback (`qsort` comparator, `pthread_create` start routine, `dispatch_async` block) up to a
   catch in game code must work, or at least terminate loudly, never corrupt state (Task 3
   fixture).
3. **errno.** `errno = 0; strtol(...)` followed by a check, a failing call reported with
   Darwin's number, and a successful call that does not change errno all behave as on Darwin
   (Task 4).
4. **Structs the game owns.** `stat`, `dirent`, `sockaddr`, `addrinfo` are allocated by the game
   in Darwin size. Shims never write past the Darwin size and never hand a Darwin buffer to
   bionic as-is (Task 5/7 tests with guard bytes).
5. **16 KB pages.** Segment copy and `mprotect` are page-correct when the page size is 16 KB, and
   the image's 16 KB-aligned segments map at their offsets (Task 3 run on the API 37 image;
   the API 28 image is checked once in Task 9).

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/android/fetch_sdk.sh`, `fetch_llvm_runtimes.sh`, `emulator.sh`, `adb_run.sh` | toolchain, emulator, run a binary on the device and collect its output and exit code |
| `tools/android/darwin_abi_gen.c` | Mac-side generator of `android/launcher/darwin/darwin_abi.h` (constants, sizes, offsets) and `darwin_ctype.inc` (C-locale `_RuneLocale` tables) |
| `android/launcher/runtime/` | `__config_site`, `__assertion_handler`, `__external_threading` for libc++; `libunwind_android.patch`; `runtime.mk` build rules |
| `android/launcher/darwin/*.cpp, *.c, *.h` | libSystem layer: `symbols.cpp` (table), `stdio.c` (variadics), `errno.cpp`, `files.cpp`, `pthread.cpp`, `mach.cpp`, `dispatch.cpp`, `blocks.cpp`, `net.cpp`, `locale.cpp`, `misc.cpp` |
| `android/launcher/loader_android.h/.cpp` | `AndroidLoaderOS` |
| `android/launcher/run.cpp` | `mcfm-run <image> [--initializers-only] [--call <symbol>]` |
| `android/launcher/tests/*.cpp` | NDK-native unit tests run on the emulator (runtime, pthread, errno, files, net) |
| `tools/tests/darwin_conformance.cpp` | Mach-O fixture exercising every shim area and libc++. It prints a transcript, which must match between macOS and Android |
| `tools/tests/android_launcher_test.sh` | `make android-test` driver: builds, pushes, runs each test and fixture on the emulator |
| `Makefile` | `android-sdk`, `llvm-runtimes`, `android-emulator`, `android-runtime`, `android-test`, `android-boot-check`, `darwin-abi` |
| docs | LAUNCHER.md Stage 3a ☑, research findings, HANDOFF, CLAUDE.md commands |

Branch: `claude/android-3` (from `main`).

---

### Task 1: Toolchain, emulator and device runner

**Files:**
- create `tools/android/adb_run.sh` and `tools/tests/fetch_sdk_test.sh`;
- modify `tools/android/fetch_sdk.sh` (`FETCH_SDK_BASE`, `FETCH_SDK_PACKAGES` overrides for the
  test), `fetch_llvm_runtimes.sh` (`FETCH_LLVM_BASE`), `emulator.sh`, `Makefile`, `.gitignore`.

**Interfaces:**
- Produces: make variables `ANDROID_SDK ?= ~/Library/Android/sdk`,
  `NDK64 ?= $(ANDROID_SDK)/ndk/27.3.13750724`, `LLVM_RUNTIMES ?= build/llvm-runtimes`,
  `ACC/ACXX` = NDK `aarch64-linux-android28-clang(++)`.
- Produces: targets `android-sdk`, `llvm-runtimes`, `android-emulator [API=37|28]`,
  `android-emulator-stop`.
- Produces: `adb_run.sh <local binary> [args...]`. It pushes to `/data/local/tmp/mcfm/`, runs,
  prints stdout/stderr, and exits with the remote exit code (adb's own code is not used).

- [ ] Test first (`fetch_sdk_test.sh`, offline, `file://` base):
  - a good zip is unpacked under its path;
  - a wrong SHA-1 leaves nothing;
  - a zip without the expected top directory is refused;
  - a second run keeps a present package.
  Same three cases for `fetch_llvm_runtimes.sh` with a local tarball.
- [ ] Watch it fail (no overrides yet), then add the overrides, the make targets and `adb_run.sh`.
- [ ] `make test` green. On the emulator, `adb_run.sh` of a program exiting 3 returns 3.
- [ ] Commit: "Android: arm64 toolchain, emulator and device runner (Stage 3a)".

### Task 2: Darwin ABI generator and the pthread layer

**Files:**
- create `tools/android/darwin_abi_gen.c`, `android/launcher/darwin/darwin_abi.h` (generated),
  `darwin_ctype.inc` (generated);
- create `android/launcher/darwin/pthread.cpp`, `darwin.h`, `android/launcher/tests/pthread_test.cpp`;
- create `tools/tests/darwin_abi_test.sh`; Makefile `darwin-abi`.

**Interfaces:**
- Produces `darwin_abi.h`: `namespace darwin { constexpr int kEAGAIN = 35; … }` for every errno,
  `O_*`, `F_*`, `FD_CLOEXEC`, `MAP_*`, `PROT_*`, `MS_*`, `_SC_*` used, `AF_*`, `SOCK_*`,
  `SOL_SOCKET`, `SO_*`, `IPPROTO_*`, `TCP_NODELAY`, `MSG_*`, `SIG*`, `SA_*`, `LC_*`/`LC_*_MASK`,
  `PTHREAD_*` constants, the static-initializer signatures (`_PTHREAD_MUTEX_SIG_init`,
  recursive and errorcheck variants, `_PTHREAD_COND_SIG_init`, `_PTHREAD_ONCE_SIG_init`), and
  `kSizeof*` / `kOffsetof*` for `stat`, `dirent`, `sockaddr`, `sockaddr_in`, `sockaddr_in6`,
  `sockaddr_un`, `addrinfo`, `hostent`, `ifaddrs`, `sigaction`, `utsname`, `termios`, `tm`,
  `timeval`, `timespec` and the pthread types. Darwin layouts are spelled as plain structs
  (`darwin::stat { uint64_t … }`) with `static_assert`s against the generated sizes and offsets.
- Produces (`darwin.h`, `extern "C"`): `mcfm_darwin_pthread_mutex_init/lock/trylock/unlock/destroy`,
  `…_cond_*`, `…_once`, `…_key_create/delete/getspecific/setspecific`, `…_create/join/detach/self`,
  `…_attr_*`, `…_mutexattr_*`, `…_condattr_*`, `…_setname_np(const char *)`, `sched_yield`.
- Storage design: a Darwin object is `{long sig; void *impl; …}`. `impl` points to a bionic
  object allocated on first use. A static-initializer signature is converted once by CAS
  `sig: INIT → BUSY → READY` (losers spin with `sched_yield`); destroy frees `impl`.
  `pthread_once` uses our own atomic state machine (no bionic object). `pthread_key_t` is zero
  extended to 8 bytes. attr, mutexattr and condattr store their fields inside the Darwin bytes
  and are translated at use.

- [ ] Test first: `darwin_abi_test.sh` regenerates the header with the Mac compiler and diffs
  it against the committed copy. RED: no generator.
- [ ] Write the generator, commit its output, and run it to GREEN.
- [ ] Test first (`pthread_test.cpp`, NDK, run with `adb_run.sh`). Cases:
  - a static Darwin mutex locked by 8 threads × 100k increments gives exact counts;
  - a recursive static mutex can be locked twice;
  - a static cond with producer/consumer loses no wakeups (timed wait returns `ETIMEDOUT`
    mapped to Darwin's 60);
  - a once routine runs once under 8 racing threads;
  - a key destructor runs on thread exit;
  - a detached-state attr creates a detached thread;
  - `setname_np` names the thread (`/proc/self/task/*/comm`);
  - guard bytes after every Darwin object stay intact.
- [ ] Implement `pthread.cpp` (bionic underneath, Darwin errno numbers returned), then GREEN on
  the emulator.
- [ ] Commit: "Android: Darwin ABI tables and pthread layer".

### Task 3: Apple-ABI runtime (libc++, libc++abi, libunwind) and the Android loader OS

**Files:**
- create `android/launcher/runtime/{__config_site,__assertion_handler,__external_threading,libunwind_android.patch,runtime.mk}`;
- create `android/launcher/loader_android.h/.cpp`, `android/launcher/run.cpp`,
  `android/launcher/darwin/symbols.cpp`;
- create `android/launcher/tests/runtime_test.cpp`; Makefile `android-runtime`.

**Interfaces:**
- `libmcfm_runtime.so` is built `-nostdlib++ -fsigned-char -fexceptions`. It contains:
  - our libc++ (the sources from `$(LLVM_RUNTIMES)/libcxx/src` that the Apple build compiles;
    the filesystem library is left out);
  - libc++abi;
  - libunwind with the patch: `_LIBUNWIND_SUPPORT_COMPACT_UNWIND` on Android, and the dynamic
    finders consulted first in `findUnwindSections`;
  - the Darwin layer.
- `__config_site`: `_LIBCPP_ABI_VERSION 1`, `_LIBCPP_ABI_NAMESPACE __1`,
  `_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`, `_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION 3`,
  `_LIBCPP_HAS_THREAD_API_EXTERNAL`; `__external_threading` maps `__libcpp_*` to the Task 2
  functions over the Darwin types.
- Ctype: libc++'s `ctype_base` masks and `classic_table()` use the Darwin values and
  `darwin_ctype.inc`. This is a patch to `__locale` / `locale.cpp` selecting the Apple branch,
  kept in `runtime/libcxx_darwin_ctype.patch`.
- `AndroidLoaderOS(const std::string &dir, void *runtime)` implements `LoaderOS`:
  - `requires_code_signature()` false, `register_code_signature` true;
  - `map_file` = anonymous `MAP_FIXED` RW mapping in the reserved range, `pread` the
    segment, then `mprotect` to `prot` right away. Fixups only write to writable segments
    (`__DATA*` have `initprot` RW, as on macOS), so no later protection step is needed and
    `shared/loader` is unchanged;
  - `open_library`: `libSystem`/`libc++` → runtime handle, `libz` → `dlopen("libz.so")`,
    `mcfm_stub_*` → `dir/mcfm_stub_X.so`;
  - `symbol`: libSystem → `mcfm_darwin_symbol(name)`, otherwise `dlsym`;
  - `flat_symbol`: the Darwin table, then the runtime, then `RTLD_DEFAULT`;
  - `register_unwind` → `__unw_add_find_dynamic_unwind_sections` (ours).
- `mcfm-run`: loads the image, prints `mcfm: loaded (slide 0x…)` and
  `mcfm: N initializers ran`. With `--call sym` it calls an exported `int sym(void)` and prints
  its result. `LoadOptions` gains `size_t *initializers_run` (set by the loader).

- [ ] Test first, `loader_core_test` (host): `initializers_run` counts 2 for a crafted image
  with two initializers. RED, then implement in `shared/loader`, GREEN.
- [ ] Test first, `runtime_test.cpp` (NDK, compiled against our libc++ headers, on the
  emulator):
  - a long `std::string` is heap-allocated and a short one inline at the Apple alternate-layout
    offsets: the size byte is the last byte, as on the Mac (the same check compiled on the Mac
    gives the same bytes);
  - an exception crosses 5 frames;
  - `std::mutex` / `std::condition_variable` / `std::thread` round-trip;
  - `std::ctype<char>::is(space, ' ')` is true via the Darwin table;
  - `typeid` equality uses the NonUnique bit (two `type_info` with the same name and the high bit
    set compare equal).
  - Also: the runtime exports all 285 libc++ import names; `nm -D` is checked against
    `dist/launcher/imports.tsv` when present, else against the LLVM Apple abilist subset listed
    in `runtime/required_symbols.txt` (generated once from the game, names only).
- [ ] Build the runtime and run GREEN on the API 37 image.
- [ ] Test first, the device fixtures:
  - the spike's C fixture (`fx.dylib`: `strlen`, an initializer) through `mcfm-run --call`;
  - the existing `loader_fixture.sh` image on Android: `thrower=41`, the hook replaced by a
    function in `mcfm-run` (`answer=1005`), `init_answer=1003`, `operator new` = runtime's.
  RED until `symbols.cpp` lists the fixture's imports, then GREEN.
- [ ] Commit: "Android: Apple-ABI libc++ runtime and our loader on Android".

### Task 4: stdio, variadics, errno, string and memory, process

**Files:** create `android/launcher/darwin/stdio.c`, `errno.cpp`, `misc.cpp`, and extend
`symbols.cpp`; create `tools/tests/darwin_conformance.cpp` (part 1) and
`tools/tests/android_launcher_test.sh`.

**Interfaces:**
- `___error()` returns a thread-local Darwin errno. If bionic's errno is nonzero, it is
  translated, stored, and bionic's reset to 0. Writes by the game are therefore kept until the
  next failing call. Each shim that fails sets it directly.
- Variadics: `printf`, `fprintf`, `sprintf`, `snprintf`, `__snprintf_chk`, `asprintf`, `sscanf`,
  `fscanf`, `open` (mode), `fcntl` (arg), `ioctl` (arg). va_list: `vfprintf`, `vsnprintf`.
- Data: `___stdinp`/`___stderrp` hold bionic's `stdin`/`stderr`; `___stack_chk_guard`
  aliases bionic's.
- Pass-through list (bionic `dlsym` at table build): string and memory, math, `malloc` family,
  `qsort`, `rand`/`srand`, `strto*`, `ato*`, `time`/`clock`/`difftime`/`gettimeofday`, the
  `tm` functions, `getpid`/`getuid`/`geteuid`/`getenv`, `sleep`/`usleep`, `FILE *` functions,
  `inet_*`, `__*_chk`, `__cxa_atexit`, `abort`. `__Unwind_Resume` → the runtime's.
- Darwin-only: `memset_pattern16`, `__sincos_stret`, `__sincosf_stret`, `__signbitf`,
  `__assert_rtn`, `OSMemoryBarrier`, `strerror`/`strerror_r` (Darwin numbers in, Darwin
  messages for the common ones, else bionic's message for the mapped number).

- [ ] Test first: the conformance fixture part 1 prints:
  - `snprintf` with int/long/double/char*/`%%`/`%*d`;
  - `sscanf` of three types;
  - `asprintf`;
  - errno after `strtol("99999999999999999999")` (34, ERANGE) and after `errno = 0` with a
    successful call;
  - `open` of a missing file with `O_CREAT|O_EXCL` twice (17, EEXIST);
  - `memset_pattern16`;
  - `sincos`;
  - `qsort` with a comparator that throws, caught in the fixture (Review Focus 2).
  `android_launcher_test.sh` runs it under our loader on macOS (golden) and on the emulator and
  diffs. RED (missing symbols).
- [ ] Implement, GREEN; `make test` green.
- [ ] Commit: "Android: Darwin stdio, variadics and errno".

### Task 5: Files, memory mapping, system information, signals, time

**Files:** create `android/launcher/darwin/files.cpp`, `system.cpp`; extend the fixture (part 2)
and `symbols.cpp`; create `android/launcher/tests/files_test.cpp`.

**Interfaces:**
- `stat`/`fstat`/`lstat` into `darwin::stat` (`st_mode` bits are the same and asserted;
  timespecs, `st_blksize`, `st_blocks`, flags 0).
- `opendir` returns a wrapper `{DIR *, darwin::dirent buf}`. `readdir`/`readdir_r` fill the
  Darwin `dirent` (`d_namlen`, `d_type`, name ≤ 1023 bytes); `closedir` frees it.
- `open`/`fcntl` translate `O_*` (`O_CREAT` 0x200 vs 0x40, and so on) and `F_*`
  (`F_GETFL`/`F_SETFL`, `F_GETFD`/`F_SETFD`, `F_SETLK*`, `F_NOCACHE` → no-op, `F_FULLFSYNC` →
  `fsync`).
- `mmap` translates `MAP_ANON` 0x1000, `MAP_PRIVATE`/`SHARED`/`FIXED`, `PROT_*`.
- `sysconf`: `_SC_NPROCESSORS_ONLN`/`CONF`, `_SC_PAGESIZE`, `_SC_PHYS_PAGES`; others →
  `-1`/EINVAL, logged once.
- `uname` → Darwin `utsname` (5 × 256), `machine` "arm64".
- `sysctlbyname` supports `hw.ncpu`, `hw.memsize`, `hw.machine` ("iPhone8,1" stays out:
  "arm64"), `hw.physicalcpu`, `kern.osversion`; others → ENOENT, logged once.
- `access`, `mkdir`, `rename` and the like pass through.
- Signals: `sigaction`/`signal`/`raise` translate numbers and the `sigaction` layout (handler,
  mask 32-bit ↔ `sigset_t`, flags).
- `tcgetattr`/`tcsetattr` → ENOTTY.
- `mktime`/`timegm`/`strptime`/`strftime` pass through (same `tm`).

- [ ] Test first: `files_test.cpp`, with guard bytes after every Darwin struct:
  - `stat` of a regular file, a directory and a symlink (`lstat`);
  - `readdir` lists the names and types of 300 files in a directory;
  - `O_APPEND`/`O_TRUNC`/`O_NONBLOCK` round-trip through `fcntl`;
  - anonymous `mmap`;
  - `SIGUSR1` (Darwin 30) delivered to a handler installed with Darwin numbers.
  Fixture part 2 prints the same through game-style Darwin code. RED.
- [ ] Implement, GREEN on the emulator; the conformance diff is clean.
- [ ] Commit: "Android: Darwin files, mmap, system information and signals".

### Task 6: Mach, blocks and dispatch

**Files:** create `android/launcher/darwin/mach.cpp`, `blocks.cpp`, `dispatch.cpp`; fixture part 3.

**Interfaces:**
- `mach_absolute_time` = `CLOCK_MONOTONIC` in ns, and `mach_timebase_info` = {1, 1}.
- `mach_task_self_` and `mach_host_self` are constants. `host_page_size` is the page size.
  `host_statistics(HOST_VM_INFO)` fills free and active counts from `/proc/meminfo`.
- `semaphore_create/signal/wait/destroy` use a bionic `sem_t` behind the handle.
- Blocks: `_Block_copy`/`_Block_release`, `_Block_object_assign`/`_dispose`, implemented from
  the Block ABI spec (`BLOCK_FIELD_IS_OBJECT` → no-op, since the ObjC runtime is stubbed;
  `IS_BLOCK` → copy; `IS_BYREF` → byref copy with forwarding). `_NSConcreteGlobalBlock`/
  `StackBlock` (and `Malloc`) are data symbols.
- Dispatch:
  - `dispatch_get_global_queue` returns a shared thread pool (`ncpu` workers);
  - `dispatch_queue_create` makes a serial queue with one worker on demand, or a concurrent
    queue backed by the pool;
  - `_dispatch_main_q` is a serial queue drained by `mcfm_darwin_drain_main_queue()` (the host
    loop; `mcfm-run` drains after initializers);
  - `dispatch_async(_f)`, `dispatch_sync` (enqueue and wait; on the current serial queue it
    deadlocks, as on Darwin, and the shim logs it), `dispatch_once` (atomic predicate,
    Darwin's `~0l` done value), `dispatch_semaphore_*`.
  - Blocks are copied on enqueue and released after running.
- `kqueue`/`kevent` → -1/ENOSYS logged once. `hash_create`/`hash_search` → NULL, logged once.
  `_dyld_register_func_for_add_image` calls back once with the game's header and slide (from
  `AndroidLoaderOS`).

- [ ] Test first, fixture part 3:
  - `dispatch_once` with 8 racing threads;
  - `dispatch_async` on a serial queue keeps order across 1,000 blocks capturing a `__block`
    counter;
  - `dispatch_sync` returns a value;
  - a block that throws inside `dispatch_async` is caught in the block (no crossing);
  - a dispatch semaphore wait with timeout;
  - `mach_absolute_time` is monotonic;
  - mach semaphores ping-pong between two threads.
  RED.
- [ ] Implement, GREEN; conformance diff clean.
- [ ] Commit: "Android: mach time, semaphores, blocks and dispatch".

### Task 7: Network

**Files:** create `android/launcher/darwin/net.cpp`, `android/launcher/tests/net_test.cpp`;
fixture part 4.

**Interfaces:**
- `sockaddr` conversion both ways (`sa_len`/`sa_family` 1+1 bytes vs a 2-byte family; `AF_INET6`
  30 ↔ 10; `sockaddr_in6` and `sockaddr_un` sizes). This covers `bind`, `connect`, `accept`,
  `getsockname`, `getpeername`, `sendto`, `recvfrom`, `sendmsg`, `recvmsg` (`msg_name` and
  control data are passed through only for `SCM_RIGHTS`, else dropped and logged).
- `socket` domain, type and protocol numbers. `getsockopt`/`setsockopt` translate the level
  (`SOL_SOCKET` 0xffff ↔ 1) and options (`SO_REUSEADDR`, `SO_REUSEPORT`, `SO_KEEPALIVE`,
  `SO_BROADCAST`, `SO_RCVBUF`, `SO_SNDBUF`, `SO_RCVTIMEO`/`SNDTIMEO` (timeval same),
  `SO_ERROR`, `SO_NOSIGPIPE` → remembered per socket and applied as `MSG_NOSIGNAL` on send,
  `IP_*`/`IPV6_*` multicast and `IPV6_V6ONLY`, `TCP_NODELAY`). Unknown options → ENOPROTOOPT,
  logged once.
- `getaddrinfo` builds Darwin `addrinfo` lists (member order `ai_canonname` before `ai_addr`;
  hints translated). `freeaddrinfo` frees ours, and `gai_strerror` maps the codes.
- `gethostbyname`/`gethostbyaddr`/`getipnodebyname`/`freehostent` return a Darwin `hostent`
  (thread-local for the non-reentrant ones). `getnameinfo` translates the flags.
- `getifaddrs`/`freeifaddrs` return a Darwin `ifaddrs` with converted sockaddrs.
  `if_nametoindex`/`if_indextoname` pass through.
- `select`: the `fd_set` bit layout is the same (asserted). `poll` events are the same
  (asserted). `in6addr_any` is data.

- [ ] Test first: `net_test.cpp`:
  - a TCP loopback echo through Darwin-layout sockaddrs (v4 and v6);
  - UDP `sendto`/`recvfrom` reports the peer address in Darwin layout;
  - `getaddrinfo("localhost", "80")` returns entries whose `ai_addr` matches `ai_family`;
  - `SO_NOSIGPIPE` then writing to a closed peer gives EPIPE (32) with no signal;
  - `getifaddrs` includes `lo` with 127.0.0.1;
  - guard bytes stay intact.
  Fixture part 4: loopback echo, `getsockname` port round-trip. RED.
- [ ] Implement, GREEN; conformance diff clean.
- [ ] Commit: "Android: Darwin sockets and name resolution".

### Task 8: Locale, ctype, CommonCrypto, and the complete libSystem table

**Files:** create `android/launcher/darwin/locale.cpp`, `crypto.cpp`;
`tools/tests/android_symbols_test.sh`.

**Interfaces:**
- `__DefaultRuneLocale` is a Darwin `_RuneLocale` filled from `darwin_ctype.inc`.
  `___maskrune(c, mask)`, `___tolower`, `___toupper`.
- `setlocale` passes through (C locale). `newlocale` translates Darwin `LC_*_MASK`;
  `uselocale`/`freelocale` pass through; `LC_GLOBAL_LOCALE` (-1) is the same and asserted.
- CommonCrypto: `CC_SHA256_Init/Update/Final` (a small SHA-256 written here, tested against
  NIST vectors), `CCHmacInit/Update/Final` (HMAC-SHA256 only, others logged), and `CCCrypt`
  (returns `kCCUnimplemented` -4. The Xbox code that uses it is not run; logged once).
- `android_symbols_test.sh` (host, `make test`): every libSystem name in
  `dist/launcher/imports.tsv` (when present), and in `runtime/required_symbols.txt`, is in
  `symbols.cpp`'s table, and the table is sorted with no duplicates.

- [ ] Test first: the symbols test lists what is missing. RED. Ctype fixture part 5:
  `isalpha`/`isspace`/`toupper` over 0–255 via the inline Darwin macros, the same transcript as
  macOS. SHA-256 and HMAC NIST vectors in `runtime_test`.
- [ ] Implement, GREEN.
- [ ] Commit: "Android: Darwin locale and ctype, CommonCrypto subset, complete libSystem table".

### Task 9: Framework stubs and the game's initializers on Android

**Files:**
- modify `tools/launcher/build_stubs.sh` (`--target android`: NDK compiler, `.so`,
  `-Wl,-soname`, `-z max-page-size=16384`, providers ignored for now) and
  `macos/launcher/stub_runtime.c` (portable logging: `__android_log_print` when `__ANDROID__`;
  `mcfm-run` also mirrors to stderr);
- Makefile `android-boot-check`; docs.

**Interfaces:**
- `make android-boot-check GAME=…`:
  1. converts the game (`make launcher` output);
  2. builds the stubs for Android;
  3. builds the runtime and `mcfm-run`;
  4. starts the API 37 emulator if needed;
  5. pushes everything and runs `mcfm-run libminecraftpe.dylib --initializers-only`.
  Expected: `mcfm: 3972 initializers ran` (the count the macOS loader reports for the same
  image, printed by `mcfm-launch --count-initializers` from the same `initializers_run`
  field).

- [ ] Test first: a launcher stubs test case builds the fixture's stubs with `--target android`.
  It checks that `readelf` shows `ELF64 AArch64`, a 16 KB `LOAD` alignment and the
  `mcfm_stub_FakeKit.so` soname. RED.
- [ ] Implement, GREEN.
- [ ] Run `make android-boot-check` on the real game (check `pgrep -x mcfm-launch` before
  touching `dist/launcher`).
  - Every crash or missing symbol found here gets a fixture or unit test first, then the fix
    (ledger each).
  - Repeat on the API 28 emulator once.
- [ ] Docs:
  - LAUNCHER.md: Stage 3a ☑ with the acceptance numbers;
  - research: findings (initializer census, stubs called, shims hit);
  - HANDOFF and CLAUDE.md: the Android commands.
- [ ] Commit: "Stage 3a: the game loads and initializes on Android".

## Acceptance

- `make test` is green without an SDK.
- `make android-test` is green on the API 37 16 KB emulator: unit tests and fixtures, with the
  conformance transcript identical between macOS and Android.
- `make android-boot-check` reports all of the game's initializers ran on API 37 and on API 28.
