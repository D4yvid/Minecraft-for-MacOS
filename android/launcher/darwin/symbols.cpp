// The libSystem table (docs/LAUNCHER.md, Stage 3a): every libSystem symbol the image may import,
// by its name without the Mach-O leading underscore. Each one is either
//   SHIM:    our Darwin-behaviour implementation,
//   BIONIC:  bionic's function or variable of the same name (same ABI, asserted per area),
//   RUNTIME: this runtime's own export (libc++abi / libunwind, e.g. _Unwind_Resume).
// Anything else is missing: the load fails naming it. Sorted by name (tools/tests check it).
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "darwin.h"

// Shims defined in the other files of this directory.
extern "C" {
int *mcfm_darwin___error(void);
int mcfm_darwin___snprintf_chk(char *, size_t, int, size_t, const char *, ...);
struct mcfm_darwin_sincos { double sinval, cosval; };
struct mcfm_darwin_sincosf { float sinval, cosval; };
mcfm_darwin_sincos mcfm_darwin___sincos_stret(double);
mcfm_darwin_sincosf mcfm_darwin___sincosf_stret(float);
int mcfm_darwin___signbitf(float);
void mcfm_darwin___assert_rtn(const char *, const char *, int, const char *);
void mcfm_darwin_OSMemoryBarrier(void);
void mcfm_darwin_bzero(void *, size_t);
int mcfm_darwin_asprintf(char **, const char *, ...);
int mcfm_darwin_fcntl(int, int, ...);
int mcfm_darwin_fprintf(FILE *, const char *, ...);
int mcfm_darwin_fscanf(FILE *, const char *, ...);
int mcfm_darwin_ioctl(int, unsigned long, ...);
void mcfm_darwin_memset_pattern16(void *, const void *, size_t);
int mcfm_darwin_open(const char *, int, ...);
void mcfm_darwin_perror(const char *);
int mcfm_darwin_printf(const char *, ...);
int mcfm_darwin_snprintf(char *, size_t, const char *, ...);
int mcfm_darwin_sprintf(char *, const char *, ...);
int mcfm_darwin_sscanf(const char *, const char *, ...);
char *mcfm_darwin_strerror(int);
int mcfm_darwin_strerror_r(int, char *, size_t);
int mcfm_darwin_vfprintf(FILE *, const char *, char *);
int mcfm_darwin_vsnprintf(char *, size_t, const char *, char *);
extern FILE *mcfm_darwin_stdinp, *mcfm_darwin_stdoutp, *mcfm_darwin_stderrp;
extern uintptr_t mcfm_darwin_stack_chk_guard;
}

namespace {

enum Kind { SHIM, BIONIC, RUNTIME };
struct Export {
  const char *name;
  Kind kind;
  void *address;  // SHIM only
};

#define S(name, fn) {name, SHIM, reinterpret_cast<void *>(fn)},
#define B(name) {name, BIONIC, nullptr},
#define R(name) {name, RUNTIME, nullptr},

const Export kExports[] = {
    S("OSMemoryBarrier", mcfm_darwin_OSMemoryBarrier)
    R("_Unwind_Resume")
    S("__assert_rtn", mcfm_darwin___assert_rtn)
    B("__cxa_atexit")
    S("__error", mcfm_darwin___error)
    B("__memcpy_chk")
    B("__memmove_chk")
    B("__memset_chk")
    S("__signbitf", mcfm_darwin___signbitf)
    S("__sincos_stret", mcfm_darwin___sincos_stret)
    S("__sincosf_stret", mcfm_darwin___sincosf_stret)
    S("__snprintf_chk", mcfm_darwin___snprintf_chk)
    B("__stack_chk_fail")
    S("__stack_chk_guard", &mcfm_darwin_stack_chk_guard)
    S("__stderrp", &mcfm_darwin_stderrp)
    S("__stdinp", &mcfm_darwin_stdinp)
    S("__stdoutp", &mcfm_darwin_stdoutp)
    B("__strcat_chk")
    B("abort")
    B("access")
    B("acos")
    B("acosf")
    B("asin")
    B("asinf")
    S("asprintf", mcfm_darwin_asprintf)
    B("atan2")
    B("atan2f")
    B("atanf")
    B("atof")
    B("atoi")
    S("bzero", mcfm_darwin_bzero)
    B("calloc")
    B("clock")
    B("close")
    B("cos")
    B("cosf")
    B("difftime")
    B("exp")
    B("exp2")
    B("exp2f")
    B("fclose")
    S("fcntl", mcfm_darwin_fcntl)
    B("feof")
    B("ferror")
    B("fflush")
    B("fgets")
    B("fileno")
    B("fmod")
    B("fmodf")
    B("fopen")
    S("fprintf", mcfm_darwin_fprintf)
    B("fputc")
    B("fputs")
    B("fread")
    B("free")
    B("frexp")
    S("fscanf", mcfm_darwin_fscanf)
    B("fseek")
    B("fseeko")
    B("fsync")
    B("ftell")
    B("ftello")
    B("fwrite")
    B("getenv")
    B("geteuid")
    B("getpid")
    B("gettimeofday")
    B("getuid")
    B("gmtime")
    B("gmtime_r")
    B("inet_addr")
    B("inet_ntoa")
    S("ioctl", mcfm_darwin_ioctl)
    B("ldexp")
    B("localtime")
    B("localtime_r")
    B("log")
    B("log10")
    B("log10f")
    B("logf")
    B("malloc")
    B("memchr")
    B("memcmp")
    B("memcpy")
    B("memmove")
    B("memset")
    S("memset_pattern16", mcfm_darwin_memset_pattern16)
    B("mkdir")
    B("mktime")
    B("munmap")
    S("open", mcfm_darwin_open)
    S("perror", mcfm_darwin_perror)
    B("pipe")
    B("pow")
    B("powf")
    B("pread")
    S("printf", mcfm_darwin_printf)
    S("pthread_attr_destroy", mcfm_darwin_pthread_attr_destroy)
    S("pthread_attr_init", mcfm_darwin_pthread_attr_init)
    S("pthread_attr_setdetachstate", mcfm_darwin_pthread_attr_setdetachstate)
    S("pthread_attr_setschedparam", mcfm_darwin_pthread_attr_setschedparam)
    S("pthread_attr_setschedpolicy", mcfm_darwin_pthread_attr_setschedpolicy)
    S("pthread_attr_setstacksize", mcfm_darwin_pthread_attr_setstacksize)
    S("pthread_cond_broadcast", mcfm_darwin_pthread_cond_broadcast)
    S("pthread_cond_destroy", mcfm_darwin_pthread_cond_destroy)
    S("pthread_cond_init", mcfm_darwin_pthread_cond_init)
    S("pthread_cond_signal", mcfm_darwin_pthread_cond_signal)
    S("pthread_cond_timedwait", mcfm_darwin_pthread_cond_timedwait)
    S("pthread_cond_wait", mcfm_darwin_pthread_cond_wait)
    S("pthread_condattr_destroy", mcfm_darwin_pthread_condattr_destroy)
    S("pthread_condattr_init", mcfm_darwin_pthread_condattr_init)
    S("pthread_create", mcfm_darwin_pthread_create)
    S("pthread_detach", mcfm_darwin_pthread_detach)
    S("pthread_getspecific", mcfm_darwin_pthread_getspecific)
    S("pthread_join", mcfm_darwin_pthread_join)
    S("pthread_key_create", mcfm_darwin_pthread_key_create)
    S("pthread_key_delete", mcfm_darwin_pthread_key_delete)
    S("pthread_mutex_destroy", mcfm_darwin_pthread_mutex_destroy)
    S("pthread_mutex_init", mcfm_darwin_pthread_mutex_init)
    S("pthread_mutex_lock", mcfm_darwin_pthread_mutex_lock)
    S("pthread_mutex_trylock", mcfm_darwin_pthread_mutex_trylock)
    S("pthread_mutex_unlock", mcfm_darwin_pthread_mutex_unlock)
    S("pthread_mutexattr_destroy", mcfm_darwin_pthread_mutexattr_destroy)
    S("pthread_mutexattr_init", mcfm_darwin_pthread_mutexattr_init)
    S("pthread_mutexattr_settype", mcfm_darwin_pthread_mutexattr_settype)
    S("pthread_once", mcfm_darwin_pthread_once)
    S("pthread_self", mcfm_darwin_pthread_self)
    S("pthread_setname_np", mcfm_darwin_pthread_setname_np)
    S("pthread_setspecific", mcfm_darwin_pthread_setspecific)
    B("putchar")
    B("puts")
    B("qsort")
    B("rand")
    B("read")
    B("realloc")
    B("remove")
    B("rename")
    B("rmdir")
    B("sched_yield")
    B("sin")
    B("sinf")
    B("sleep")
    S("snprintf", mcfm_darwin_snprintf)
    S("sprintf", mcfm_darwin_sprintf)
    B("srand")
    S("sscanf", mcfm_darwin_sscanf)
    B("strcasecmp")
    B("strcat")
    B("strchr")
    B("strcmp")
    B("strcpy")
    B("strcspn")
    B("strdup")
    S("strerror", mcfm_darwin_strerror)
    S("strerror_r", mcfm_darwin_strerror_r)
    B("strftime")
    B("strlen")
    B("strncasecmp")
    B("strncat")
    B("strncmp")
    B("strncpy")
    B("strpbrk")
    B("strptime")
    B("strrchr")
    B("strspn")
    B("strstr")
    B("strtod")
    B("strtol")
    B("strtoul")
    B("strtoull")
    B("tan")
    B("tanf")
    B("time")
    B("timegm")
    B("unlink")
    B("usleep")
    S("vfprintf", mcfm_darwin_vfprintf)
    S("vsnprintf", mcfm_darwin_vsnprintf)
    B("write")
};

#undef S
#undef B
#undef R

int compare(const void *key, const void *entry) {
  return strcmp(static_cast<const char *>(key), static_cast<const Export *>(entry)->name);
}

void *runtime_handle() {
  static void *handle = [] {
    Dl_info info;
    return dladdr(reinterpret_cast<void *>(&compare), &info) ? dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD) : nullptr;
  }();
  return handle;
}

}  // namespace

extern "C" void *mcfm_darwin_symbol(const char *name) {
  const Export *e = static_cast<const Export *>(
      bsearch(name, kExports, sizeof kExports / sizeof kExports[0], sizeof kExports[0], compare));
  if (!e) return nullptr;
  if (e->kind == SHIM) return e->address;
  if (e->kind == RUNTIME) return runtime_handle() ? dlsym(runtime_handle(), name) : nullptr;
  return dlsym(RTLD_DEFAULT, name);
}

extern "C" size_t mcfm_darwin_symbol_names(const char **names, size_t capacity) {
  size_t n = sizeof kExports / sizeof kExports[0];
  for (size_t i = 0; i < n && i < capacity; i++) names[i] = kExports[i].name;
  return n;
}
