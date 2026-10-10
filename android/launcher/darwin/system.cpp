// Darwin file, memory-mapping, system-information, signal and terminal APIs on bionic: Darwin's
// structs (stat, dirent, utsname, sigaction, siginfo) and numbers (MAP_*, _SC_*, signals,
// SA_*), filled from bionic's. Failures leave bionic's errno set; ___error translates it.
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>

#include "darwin.h"

static_assert(darwin::kS_IFMT == S_IFMT && darwin::kS_IFDIR == S_IFDIR && darwin::kS_IFREG == S_IFREG &&
                  darwin::kS_IFLNK == S_IFLNK && darwin::kS_IFIFO == S_IFIFO && darwin::kS_IFSOCK == S_IFSOCK,
              "st_mode file types are the same");
static_assert(darwin::kDT_DIR == DT_DIR && darwin::kDT_REG == DT_REG && darwin::kDT_LNK == DT_LNK &&
                  darwin::kDT_UNKNOWN == DT_UNKNOWN,
              "d_type values are the same");
static_assert(darwin::kPROT_READ == PROT_READ && darwin::kPROT_WRITE == PROT_WRITE && darwin::kPROT_EXEC == PROT_EXEC &&
                  darwin::kPROT_NONE == PROT_NONE,
              "PROT_* are the same");

namespace {

void to_darwin(const struct stat &b, darwin::stat *d) {
  memset(d, 0, sizeof *d);
  d->st_dev = static_cast<int32_t>(b.st_dev);
  d->st_mode = static_cast<uint16_t>(b.st_mode);
  d->st_nlink = static_cast<uint16_t>(b.st_nlink);
  d->st_ino = b.st_ino;
  d->st_uid = b.st_uid;
  d->st_gid = b.st_gid;
  d->st_rdev = static_cast<int32_t>(b.st_rdev);
  d->st_atimespec = {b.st_atim.tv_sec, b.st_atim.tv_nsec};
  d->st_mtimespec = {b.st_mtim.tv_sec, b.st_mtim.tv_nsec};
  d->st_ctimespec = {b.st_ctim.tv_sec, b.st_ctim.tv_nsec};
  d->st_birthtimespec = d->st_ctimespec;  // Linux has no birth time here: the change time
  d->st_size = b.st_size;
  d->st_blocks = b.st_blocks;
  d->st_blksize = static_cast<int32_t>(b.st_blksize);
}

// A Darwin DIR*: bionic's DIR and the dirent readdir() returns into.
struct Dir {
  DIR *dir;
  darwin::dirent entry;
};

void to_darwin(const struct dirent &b, darwin::dirent *d) {
  d->d_ino = b.d_ino;
  d->d_seekoff = static_cast<uint64_t>(b.d_off);
  d->d_reclen = sizeof(darwin::dirent);
  d->d_type = b.d_type;
  size_t n = strnlen(b.d_name, sizeof d->d_name - 1);
  memcpy(d->d_name, b.d_name, n);
  d->d_name[n] = 0;
  d->d_namlen = static_cast<uint16_t>(n);
}

// Signals: Darwin's numbers in the game, bionic's in the kernel.
struct SignalPair { int darwin, bionic; };
const SignalPair kSignals[] = {
#define MCFM_PAIR(name, value) {value, name},
    MCFM_DARWIN_SIGNALS(MCFM_PAIR)
#undef MCFM_PAIR
};

struct FlagPair { long darwin; unsigned bionic; };  // SA_RESETHAND is bit 31 on bionic
const FlagPair kSigactionFlags[] = {
    {darwin::kSA_ONSTACK, SA_ONSTACK},     {darwin::kSA_RESTART, SA_RESTART}, {darwin::kSA_RESETHAND, SA_RESETHAND},
    {darwin::kSA_NOCLDSTOP, SA_NOCLDSTOP}, {darwin::kSA_NODEFER, SA_NODEFER}, {darwin::kSA_NOCLDWAIT, SA_NOCLDWAIT},
    {darwin::kSA_SIGINFO, SA_SIGINFO},
};

// The game's handlers, by bionic signal number; the kernel calls trampoline(), which hands the
// game Darwin's number (and a Darwin siginfo for SA_SIGINFO handlers).
constexpr int kMaxSignal = 65;
struct Handler {
  std::atomic<void (*)(int)> function;
  std::atomic<int> darwin_flags;
  std::atomic<uint32_t> darwin_mask;
};
Handler g_handlers[kMaxSignal];

void trampoline(int bionic_signo, siginfo_t *info, void *) {
  int saved = errno;
  int signo = mcfm_darwin_signal_number(bionic_signo);
  void (*function)(int) = g_handlers[bionic_signo].function.load();
  if (g_handlers[bionic_signo].darwin_flags.load() & darwin::kSA_SIGINFO) {
    darwin::siginfo d;
    memset(&d, 0, sizeof d);
    d.signo = signo;
    if (info) {
      d.error = mcfm_darwin_errno(info->si_errno);
      d.code = info->si_code;
      d.pid = info->si_pid;
      d.uid = info->si_uid;
      d.addr = info->si_addr;
    }
    // Darwin's ucontext has another layout: handlers get none.
    reinterpret_cast<void (*)(int, darwin::siginfo *, void *)>(function)(signo, &d, nullptr);
  } else {
    function(signo);
  }
  errno = saved;
}

uint32_t darwin_mask_of(const sigset_t &set) {
  uint32_t mask = 0;
  for (const SignalPair &p : kSignals)
    if (sigismember(&set, p.bionic) == 1 && p.darwin <= 32) mask |= 1u << (p.darwin - 1);
  return mask;
}

void bionic_mask_of(uint32_t mask, sigset_t *set) {
  sigemptyset(set);
  for (const SignalPair &p : kSignals)
    if (p.darwin <= 32 && (mask & (1u << (p.darwin - 1)))) sigaddset(set, p.bionic);
}

void (*const kDefault)(int) = SIG_DFL;
void (*const kIgnore)(int) = SIG_IGN;

}  // namespace

extern "C" {

int mcfm_bionic_signal(int darwin_signo) {
  for (const SignalPair &p : kSignals)
    if (p.darwin == darwin_signo) return p.bionic;
  return 0;
}

int mcfm_darwin_signal_number(int bionic_signo) {
  for (const SignalPair &p : kSignals)
    if (p.bionic == bionic_signo) return p.darwin;
  return bionic_signo;
}

int mcfm_darwin_stat(const char *path, darwin::stat *out) {
  struct stat b;
  int r = stat(path, &b);
  if (r == 0) to_darwin(b, out);
  return r;
}

int mcfm_darwin_lstat(const char *path, darwin::stat *out) {
  struct stat b;
  int r = lstat(path, &b);
  if (r == 0) to_darwin(b, out);
  return r;
}

int mcfm_darwin_fstat(int fd, darwin::stat *out) {
  struct stat b;
  int r = fstat(fd, &b);
  if (r == 0) to_darwin(b, out);
  return r;
}

void *mcfm_darwin_opendir(const char *path) {
  DIR *d = opendir(path);
  if (!d) return nullptr;
  Dir *dir = static_cast<Dir *>(calloc(1, sizeof(Dir)));
  if (!dir) {
    closedir(d);
    errno = ENOMEM;
    return nullptr;
  }
  dir->dir = d;
  return dir;
}

darwin::dirent *mcfm_darwin_readdir(void *handle) {
  Dir *dir = static_cast<Dir *>(handle);
  struct dirent *b = readdir(dir->dir);
  if (!b) return nullptr;
  to_darwin(*b, &dir->entry);
  return &dir->entry;
}

int mcfm_darwin_readdir_r(void *handle, darwin::dirent *entry, darwin::dirent **result) {
  Dir *dir = static_cast<Dir *>(handle);
  int saved = errno;
  errno = 0;
  struct dirent *b = readdir(dir->dir);  // bionic's readdir is per-DIR, as readdir_r needs
  if (!b) {
    int e = errno;
    errno = saved;
    *result = nullptr;
    return mcfm_darwin_errno(e);
  }
  errno = saved;
  to_darwin(*b, entry);
  *result = entry;
  return 0;
}

int mcfm_darwin_closedir(void *handle) {
  Dir *dir = static_cast<Dir *>(handle);
  int r = closedir(dir->dir);
  free(dir);
  return r;
}

void *mcfm_darwin_mmap(void *address, size_t length, int prot, int flags, int fd, long offset) {
  int b = 0;
  if (flags & darwin::kMAP_SHARED) b |= MAP_SHARED;
  if (flags & darwin::kMAP_PRIVATE) b |= MAP_PRIVATE;
  if (flags & darwin::kMAP_FIXED) b |= MAP_FIXED;
  if (flags & darwin::kMAP_ANON) b |= MAP_ANONYMOUS;
  long known = darwin::kMAP_SHARED | darwin::kMAP_PRIVATE | darwin::kMAP_FIXED | darwin::kMAP_ANON;
  if (flags & ~known) mcfm_darwin_log_once("mmap: Darwin-only flags ignored");
  // Darwin's anonymous mappings may pass a VM tag in fd (VM_MAKE_TAG); bionic wants -1.
  return mmap(address, length, prot, b, (flags & darwin::kMAP_ANON) ? -1 : fd, offset);
}

// bionic has getpagesize() only as an inline function in its headers.
int mcfm_darwin_getpagesize(void) { return static_cast<int>(sysconf(_SC_PAGESIZE)); }

long mcfm_darwin_sysconf(int name) {
  if (name == darwin::k_SC_NPROCESSORS_ONLN) return sysconf(_SC_NPROCESSORS_ONLN);
  if (name == darwin::k_SC_NPROCESSORS_CONF) return sysconf(_SC_NPROCESSORS_CONF);
  if (name == darwin::k_SC_PAGESIZE || name == darwin::k_SC_PAGE_SIZE) return sysconf(_SC_PAGESIZE);
  if (name == darwin::k_SC_PHYS_PAGES) return sysconf(_SC_PHYS_PAGES);
  if (name == darwin::k_SC_CLK_TCK) return sysconf(_SC_CLK_TCK);
  if (name == darwin::k_SC_OPEN_MAX) return sysconf(_SC_OPEN_MAX);
  if (name == darwin::k_SC_ARG_MAX) return sysconf(_SC_ARG_MAX);
  mcfm_darwin_log_once("sysconf: unsupported Darwin name");
  errno = EINVAL;
  return -1;
}

// Darwin's view: sysname "Darwin" and machine "arm64" (what the macOS launcher reports too);
// the host's node name, release and version.
int mcfm_darwin_uname(darwin::utsname *out) {
  struct utsname b;
  if (uname(&b) != 0) return -1;
  memset(out, 0, sizeof *out);
  strcpy(out->sysname, "Darwin");
  strncpy(out->nodename, b.nodename, sizeof out->nodename - 1);
  strncpy(out->release, b.release, sizeof out->release - 1);
  strncpy(out->version, b.version, sizeof out->version - 1);
  strcpy(out->machine, "arm64");
  return 0;
}

// The names the game reads (hw.machine, hw.cputype, hw.cpusubtype) and common hw.* ones. Values
// as on an arm64 Apple device; others fail with ENOENT, as an unknown name does on Darwin.
int mcfm_darwin_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t) {
  if (newp) {
    errno = EPERM;
    return -1;
  }
  char text[64];
  int32_t i32 = 0;
  int64_t i64 = 0;
  const void *value = nullptr;
  size_t size = 0;
  if (strcmp(name, "hw.machine") == 0) {
    strcpy(text, "arm64");
    value = text;
    size = strlen(text) + 1;
  } else if (strcmp(name, "hw.cputype") == 0) {
    i32 = 0x0100000C;  // CPU_TYPE_ARM64
  } else if (strcmp(name, "hw.cpusubtype") == 0) {
    i32 = 0;  // CPU_SUBTYPE_ARM64_ALL
  } else if (strcmp(name, "hw.ncpu") == 0 || strcmp(name, "hw.logicalcpu") == 0 || strcmp(name, "hw.physicalcpu") == 0 ||
             strcmp(name, "hw.activecpu") == 0) {
    i32 = static_cast<int32_t>(sysconf(_SC_NPROCESSORS_ONLN));
  } else if (strcmp(name, "hw.pagesize") == 0) {
    i64 = sysconf(_SC_PAGESIZE);
    value = &i64;
    size = sizeof i64;
  } else if (strcmp(name, "hw.memsize") == 0) {
    i64 = static_cast<int64_t>(sysconf(_SC_PHYS_PAGES)) * sysconf(_SC_PAGESIZE);
    value = &i64;
    size = sizeof i64;
  } else {
    errno = ENOENT;
    return -1;
  }
  if (!value) {
    value = &i32;
    size = sizeof i32;
  }
  if (!oldlenp) {
    errno = EINVAL;
    return -1;
  }
  if (!oldp) {
    *oldlenp = size;
    return 0;
  }
  if (*oldlenp < size) {
    errno = ENOMEM;
    return -1;
  }
  memcpy(oldp, value, size);
  *oldlenp = size;
  return 0;
}

int mcfm_darwin_sigaction(int signo, const darwin::sigaction_t *act, darwin::sigaction_t *old) {
  int b = mcfm_bionic_signal(signo);
  if (b <= 0 || b >= kMaxSignal) {
    errno = EINVAL;
    return -1;
  }
  struct sigaction current;
  if (sigaction(b, nullptr, &current) != 0) return -1;
  if (old) {
    bool ours = (current.sa_flags & SA_SIGINFO) && current.sa_sigaction == trampoline;
    old->handler = ours ? g_handlers[b].function.load() : current.sa_handler;
    old->mask = ours ? g_handlers[b].darwin_mask.load() : darwin_mask_of(current.sa_mask);
    old->flags = ours ? g_handlers[b].darwin_flags.load() : 0;
  }
  if (!act) return 0;
  struct sigaction next;
  memset(&next, 0, sizeof next);
  bionic_mask_of(act->mask, &next.sa_mask);
  for (const FlagPair &p : kSigactionFlags)
    if (act->flags & p.darwin) next.sa_flags |= static_cast<int>(p.bionic);
  if (act->handler == kDefault || act->handler == kIgnore) {
    next.sa_flags &= ~SA_SIGINFO;
    next.sa_handler = act->handler;
  } else {
    // The signal is blocked while the trampoline's record changes, so it never sees a new
    // handler with the old flags (one-argument vs SA_SIGINFO) or the reverse.
    sigset_t block, saved;
    sigemptyset(&block);
    sigaddset(&block, b);
    pthread_sigmask(SIG_BLOCK, &block, &saved);
    g_handlers[b].darwin_flags.store(act->flags);
    g_handlers[b].darwin_mask.store(act->mask);
    g_handlers[b].function.store(act->handler);
    next.sa_flags |= SA_SIGINFO;
    next.sa_sigaction = trampoline;
    int r = sigaction(b, &next, nullptr);
    pthread_sigmask(SIG_SETMASK, &saved, nullptr);
    return r;
  }
  return sigaction(b, &next, nullptr);
}

// Darwin's signal() has BSD semantics: the handler stays installed and calls restart.
void (*mcfm_darwin_signal(int signo, void (*handler)(int)))(int) {
  darwin::sigaction_t act = {handler, 0, static_cast<int>(darwin::kSA_RESTART)}, old;
  if (mcfm_darwin_sigaction(signo, &act, &old) != 0) return SIG_ERR;
  return old.handler;
}

int mcfm_darwin_raise(int signo) {
  int b = mcfm_bionic_signal(signo);
  if (b <= 0) {
    errno = EINVAL;
    return -1;
  }
  return raise(b);
}

// Terminals: Darwin's termios has another layout, and the game has no terminal on Android.
int mcfm_darwin_tcgetattr(int fd, void *) {
  if (isatty(fd)) {
    mcfm_darwin_log_once("tcgetattr: terminals are not supported");
    errno = ENOTTY;
  }
  return -1;  // isatty() set ENOTTY (or EBADF) otherwise
}

int mcfm_darwin_tcsetattr(int fd, int, const void *termios) { return mcfm_darwin_tcgetattr(fd, const_cast<void *>(termios)); }

}  // extern "C"
