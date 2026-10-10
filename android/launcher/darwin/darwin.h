#pragma once
// The Darwin libSystem layer on Android (docs/LAUNCHER.md, Stage 3a): Darwin arm64 types as the
// game (and our Apple-ABI libc++) lay them out, and the functions that implement Darwin's
// behaviour on bionic. Numbers and sizes come from the generated darwin_abi.h.
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "darwin_abi.h"

namespace darwin {

// pthread objects: Darwin's sizes; `sig` first, as Darwin's static initializers set it.
struct pthread_mutex { long sig; char opaque[56]; };
struct pthread_mutexattr { long sig; char opaque[8]; };
struct pthread_cond { long sig; char opaque[40]; };
struct pthread_condattr { long sig; char opaque[8]; };
struct pthread_once { long sig; char opaque[8]; };
struct pthread_attr { long sig; char opaque[56]; };
struct sched_param { int sched_priority; char opaque[4]; };
typedef unsigned long pthread_key;
typedef uintptr_t pthread_t;  // bionic's pthread_t value, widened

static_assert(sizeof(pthread_mutex) == kSizeof_pthread_mutex_t, "pthread_mutex_t");
static_assert(sizeof(pthread_mutexattr) == kSizeof_pthread_mutexattr_t, "pthread_mutexattr_t");
static_assert(sizeof(pthread_cond) == kSizeof_pthread_cond_t, "pthread_cond_t");
static_assert(sizeof(pthread_condattr) == kSizeof_pthread_condattr_t, "pthread_condattr_t");
static_assert(sizeof(pthread_once) == kSizeof_pthread_once_t, "pthread_once_t");
static_assert(sizeof(pthread_attr) == kSizeof_pthread_attr_t, "pthread_attr_t");
static_assert(sizeof(sched_param) == kSizeof_sched_param, "sched_param");
static_assert(sizeof(pthread_key) == kSizeof_pthread_key_t, "pthread_key_t");
static_assert(sizeof(pthread_t) == kSizeof_pthread_t, "pthread_t");

// Files and system (system.cpp).
struct timespec { long tv_sec, tv_nsec; };
struct stat {
  int32_t st_dev;
  uint16_t st_mode, st_nlink;
  uint64_t st_ino;
  uint32_t st_uid, st_gid;
  int32_t st_rdev;
  timespec st_atimespec, st_mtimespec, st_ctimespec, st_birthtimespec;
  int64_t st_size, st_blocks;
  int32_t st_blksize;
  uint32_t st_flags, st_gen;
  int32_t st_lspare;
  int64_t st_qspare[2];
};
static_assert(sizeof(stat) == kSizeof_stat, "stat");
static_assert(offsetof(stat, st_ino) == kOffsetof_stat_st_ino && offsetof(stat, st_atimespec) == kOffsetof_stat_st_atimespec &&
                  offsetof(stat, st_birthtimespec) == kOffsetof_stat_st_birthtimespec &&
                  offsetof(stat, st_size) == kOffsetof_stat_st_size && offsetof(stat, st_blksize) == kOffsetof_stat_st_blksize &&
                  offsetof(stat, st_gen) == kOffsetof_stat_st_gen,
              "stat fields");
struct dirent {
  uint64_t d_ino, d_seekoff;
  uint16_t d_reclen, d_namlen;
  uint8_t d_type;
  char d_name[1024];
};
static_assert(sizeof(dirent) == kSizeof_dirent && offsetof(dirent, d_namlen) == kOffsetof_dirent_d_namlen &&
                  offsetof(dirent, d_type) == kOffsetof_dirent_d_type && offsetof(dirent, d_name) == kOffsetof_dirent_d_name,
              "dirent");
struct utsname { char sysname[256], nodename[256], release[256], version[256], machine[256]; };
static_assert(sizeof(utsname) == kSizeof_utsname, "utsname");
// struct sigaction as user code sees it: handler (or sigaction), a 32-bit mask, flags.
struct sigaction_t {
  void (*handler)(int);
  uint32_t mask;
  int flags;
};
static_assert(sizeof(sigaction_t) == kSizeof_sigaction && offsetof(sigaction_t, mask) == kOffsetof_sigaction_sa_mask &&
                  offsetof(sigaction_t, flags) == kOffsetof_sigaction_sa_flags,
              "sigaction");
// Darwin's siginfo_t (bionic defines the si_* names as macros, so the fields are renamed).
struct siginfo {
  int signo, error, code;
  int pid;
  unsigned uid;
  int status;
  void *addr;
  void *value;
  long band;
  unsigned long pad[7];
};
static_assert(sizeof(siginfo) == kSizeof_siginfo_t && offsetof(siginfo, code) == kOffsetof_siginfo_t_si_code &&
                  offsetof(siginfo, pid) == kOffsetof_siginfo_t_si_pid && offsetof(siginfo, addr) == kOffsetof_siginfo_t_si_addr &&
                  offsetof(siginfo, value) == kOffsetof_siginfo_t_si_value,
              "siginfo");

}  // namespace darwin

extern "C" {

// errno.cpp: bionic errno number -> Darwin's (a number with no Darwin name is returned unchanged).
int mcfm_darwin_errno(int bionic_errno);
// Darwin errno number -> bionic's (for strerror and for errno set by the game).
int mcfm_bionic_errno(int darwin_errno);

// pthread.cpp. Return values are Darwin errno numbers.
int mcfm_darwin_pthread_mutex_init(darwin::pthread_mutex *m, const darwin::pthread_mutexattr *attr);
int mcfm_darwin_pthread_mutex_lock(darwin::pthread_mutex *m);
int mcfm_darwin_pthread_mutex_trylock(darwin::pthread_mutex *m);
int mcfm_darwin_pthread_mutex_unlock(darwin::pthread_mutex *m);
int mcfm_darwin_pthread_mutex_destroy(darwin::pthread_mutex *m);
int mcfm_darwin_pthread_mutexattr_init(darwin::pthread_mutexattr *a);
int mcfm_darwin_pthread_mutexattr_settype(darwin::pthread_mutexattr *a, int type);
int mcfm_darwin_pthread_mutexattr_destroy(darwin::pthread_mutexattr *a);
int mcfm_darwin_pthread_cond_init(darwin::pthread_cond *c, const darwin::pthread_condattr *attr);
int mcfm_darwin_pthread_cond_wait(darwin::pthread_cond *c, darwin::pthread_mutex *m);
int mcfm_darwin_pthread_cond_timedwait(darwin::pthread_cond *c, darwin::pthread_mutex *m, const struct timespec *abstime);
int mcfm_darwin_pthread_cond_signal(darwin::pthread_cond *c);
int mcfm_darwin_pthread_cond_broadcast(darwin::pthread_cond *c);
int mcfm_darwin_pthread_cond_destroy(darwin::pthread_cond *c);
int mcfm_darwin_pthread_condattr_init(darwin::pthread_condattr *a);
int mcfm_darwin_pthread_condattr_destroy(darwin::pthread_condattr *a);
int mcfm_darwin_pthread_once(darwin::pthread_once *o, void (*routine)(void));
int mcfm_darwin_pthread_key_create(darwin::pthread_key *key, void (*destructor)(void *));
int mcfm_darwin_pthread_key_delete(darwin::pthread_key key);
void *mcfm_darwin_pthread_getspecific(darwin::pthread_key key);
int mcfm_darwin_pthread_setspecific(darwin::pthread_key key, const void *value);
int mcfm_darwin_pthread_attr_init(darwin::pthread_attr *a);
int mcfm_darwin_pthread_attr_destroy(darwin::pthread_attr *a);
int mcfm_darwin_pthread_attr_setdetachstate(darwin::pthread_attr *a, int state);
int mcfm_darwin_pthread_attr_setstacksize(darwin::pthread_attr *a, size_t size);
int mcfm_darwin_pthread_attr_setschedpolicy(darwin::pthread_attr *a, int policy);
int mcfm_darwin_pthread_attr_setschedparam(darwin::pthread_attr *a, const darwin::sched_param *param);
int mcfm_darwin_pthread_create(darwin::pthread_t *thread, const darwin::pthread_attr *attr, void *(*start)(void *), void *arg);
int mcfm_darwin_pthread_join(darwin::pthread_t thread, void **result);
int mcfm_darwin_pthread_detach(darwin::pthread_t thread);
darwin::pthread_t mcfm_darwin_pthread_self(void);
int mcfm_darwin_pthread_setname_np(const char *name);

// system.cpp. Failures leave bionic's errno set (translated by ___error).
int mcfm_darwin_stat(const char *path, darwin::stat *out);
int mcfm_darwin_lstat(const char *path, darwin::stat *out);
int mcfm_darwin_fstat(int fd, darwin::stat *out);
void *mcfm_darwin_opendir(const char *path);
darwin::dirent *mcfm_darwin_readdir(void *dir);
int mcfm_darwin_readdir_r(void *dir, darwin::dirent *entry, darwin::dirent **result);
int mcfm_darwin_closedir(void *dir);
void *mcfm_darwin_mmap(void *address, size_t length, int prot, int flags, int fd, long offset);
long mcfm_darwin_sysconf(int name);
int mcfm_darwin_getpagesize(void);
int mcfm_darwin_uname(darwin::utsname *out);
int mcfm_darwin_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int mcfm_darwin_sigaction(int signo, const darwin::sigaction_t *act, darwin::sigaction_t *old);
void (*mcfm_darwin_signal(int signo, void (*handler)(int)))(int);
int mcfm_darwin_raise(int signo);
int mcfm_darwin_tcgetattr(int fd, void *termios);
int mcfm_darwin_tcsetattr(int fd, int action, const void *termios);
// Signal numbers: Darwin <-> bionic (0 when the other system has no such signal).
int mcfm_bionic_signal(int darwin_signo);
int mcfm_darwin_signal_number(int bionic_signo);

// mach.cpp: an image our loader mapped (for _dyld_register_func_for_add_image callbacks).
void mcfm_darwin_add_image(const void *header, intptr_t slide);
// dispatch.cpp: runs the work queued on the main queue (the host loop calls it).
void mcfm_darwin_drain_main_queue(void);

// symbols.cpp: the address for a libSystem import (name without the Mach-O '_'), or null.
void *mcfm_darwin_symbol(const char *name);
// Every name in the table, sorted (for tests); returns the count.
size_t mcfm_darwin_symbol_names(const char **names, size_t capacity);

// Logs "mcfm: <message>" once per call site key (the message itself).
void mcfm_darwin_log_once(const char *message);

}  // extern "C"
