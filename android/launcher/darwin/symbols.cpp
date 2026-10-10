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

#include <__config_site>

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
int mcfm_darwin___darwin_check_fd_set_overflow(int, const void *, int);
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
// blocks.cpp, dispatch.cpp, mach.cpp
void mcfm_darwin__Block_object_assign(void *, const void *, int);
void mcfm_darwin__Block_object_dispose(const void *, int);
void *mcfm_darwin_Block_copy(const void *);
void mcfm_darwin_Block_release(const void *);
extern void *mcfm_darwin_NSConcreteGlobalBlock[32], *mcfm_darwin_NSConcreteStackBlock[32], *mcfm_darwin_NSConcreteMallocBlock[32];
extern struct mcfm_darwin_queue mcfm_darwin_dispatch_main_q;
void mcfm_darwin_dispatch_async(void *, void *);
void mcfm_darwin_dispatch_async_f(void *, void *, void (*)(void *));
void *mcfm_darwin_dispatch_get_global_queue(long, unsigned long);
void mcfm_darwin_dispatch_once(long *, void *);
void *mcfm_darwin_dispatch_queue_create(const char *, const void *);
void *mcfm_darwin_dispatch_semaphore_create(long);
long mcfm_darwin_dispatch_semaphore_signal(void *);
long mcfm_darwin_dispatch_semaphore_wait(void *, uint64_t);
void mcfm_darwin_dispatch_sync(void *, void *);
uint64_t mcfm_darwin_dispatch_time(uint64_t, int64_t);
uint64_t mcfm_darwin_mach_absolute_time(void);
uint32_t mcfm_darwin_mach_host_self(void);
extern uint32_t mcfm_darwin_mach_task_self_;
int mcfm_darwin_mach_timebase_info(uint32_t *);
int mcfm_darwin_host_page_size(uint32_t, uintptr_t *);
int mcfm_darwin_host_statistics(uint32_t, int, int *, uint32_t *);
int mcfm_darwin_semaphore_create(uint32_t, uint32_t *, int, int);
int mcfm_darwin_semaphore_destroy(uint32_t, uint32_t);
int mcfm_darwin_semaphore_signal(uint32_t);
int mcfm_darwin_semaphore_wait(uint32_t);
int mcfm_darwin_kqueue(void);
int mcfm_darwin_kevent(int, const void *, int, void *, int, const void *);
void *mcfm_darwin_hash_create(int);
void *mcfm_darwin_hash_search(void *, const void *, int, void *, void *);
void mcfm_darwin__dyld_register_func_for_add_image(void (*)(const void *, intptr_t));
// net.cpp (pointer-typed here: only the addresses are used)
void mcfm_darwin_accept(void); void mcfm_darwin_bind(void); void mcfm_darwin_close(void); void mcfm_darwin_connect(void);
void mcfm_darwin_freeaddrinfo(void); void mcfm_darwin_freehostent(void); void mcfm_darwin_freeifaddrs(void);
void mcfm_darwin_gai_strerror(void); void mcfm_darwin_getaddrinfo(void); void mcfm_darwin_gethostbyaddr(void);
void mcfm_darwin_gethostbyname(void); void mcfm_darwin_getifaddrs(void); void mcfm_darwin_getipnodebyname(void);
void mcfm_darwin_getnameinfo(void); void mcfm_darwin_getpeername(void); void mcfm_darwin_getsockname(void);
void mcfm_darwin_getsockopt(void); void mcfm_darwin_inet_ntop(void); void mcfm_darwin_inet_pton(void);
void mcfm_darwin_poll(void); void mcfm_darwin_recv(void); void mcfm_darwin_recvfrom(void); void mcfm_darwin_recvmsg(void);
void mcfm_darwin_send(void); void mcfm_darwin_sendmsg(void); void mcfm_darwin_sendto(void);
void mcfm_darwin_setsockopt(void); void mcfm_darwin_socket(void); void mcfm_darwin_write(void);
// crypto.cpp, locale.cpp
void mcfm_darwin_CCCrypt(void); void mcfm_darwin_CCHmacFinal(void); void mcfm_darwin_CCHmacInit(void);
void mcfm_darwin_CCHmacUpdate(void); void mcfm_darwin_CC_SHA256_Final(void); void mcfm_darwin_CC_SHA256_Init(void);
void mcfm_darwin_CC_SHA256_Update(void); void mcfm_darwin___maskrune(void); void mcfm_darwin___tolower(void);
void mcfm_darwin___toupper(void); void mcfm_darwin_setlocale(void); void mcfm_darwin_newlocale(void);
extern uintptr_t mcfm_darwin_stack_chk_guard;
}

namespace {

enum Kind { SHIM, BIONIC, RUNTIME };
struct Export {
  const char *name;
  Kind kind;
  void *address;  // SHIM only
};

#define S(name, fn) {name, SHIM, (void *)(fn)},  // C cast: functions, data, const data alike
#define B(name) {name, BIONIC, nullptr},
#define R(name) {name, RUNTIME, nullptr},

const Export kExports[] = {
    S("CCCrypt", mcfm_darwin_CCCrypt)
    S("CCHmacFinal", mcfm_darwin_CCHmacFinal)
    S("CCHmacInit", mcfm_darwin_CCHmacInit)
    S("CCHmacUpdate", mcfm_darwin_CCHmacUpdate)
    S("CC_SHA256_Final", mcfm_darwin_CC_SHA256_Final)
    S("CC_SHA256_Init", mcfm_darwin_CC_SHA256_Init)
    S("CC_SHA256_Update", mcfm_darwin_CC_SHA256_Update)
    S("OSMemoryBarrier", mcfm_darwin_OSMemoryBarrier)
    S("_Block_copy", mcfm_darwin_Block_copy)
    S("_Block_object_assign", mcfm_darwin__Block_object_assign)
    S("_Block_object_dispose", mcfm_darwin__Block_object_dispose)
    S("_Block_release", mcfm_darwin_Block_release)
    S("_DefaultRuneLocale", &mcfm_darwin_DefaultRuneLocale)
    S("_NSConcreteGlobalBlock", &mcfm_darwin_NSConcreteGlobalBlock)
    S("_NSConcreteMallocBlock", &mcfm_darwin_NSConcreteMallocBlock)
    S("_NSConcreteStackBlock", &mcfm_darwin_NSConcreteStackBlock)
    R("_Unwind_Resume")
    S("__assert_rtn", mcfm_darwin___assert_rtn)
    B("__cxa_atexit")
    S("__darwin_check_fd_set_overflow", mcfm_darwin___darwin_check_fd_set_overflow)
    S("__error", mcfm_darwin___error)
    S("__maskrune", mcfm_darwin___maskrune)
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
    S("__tolower", mcfm_darwin___tolower)
    S("__toupper", mcfm_darwin___toupper)
    S("_dispatch_main_q", &mcfm_darwin_dispatch_main_q)
    S("_dyld_register_func_for_add_image", mcfm_darwin__dyld_register_func_for_add_image)
    B("abort")
    S("accept", mcfm_darwin_accept)
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
    S("bind", mcfm_darwin_bind)
    S("bzero", mcfm_darwin_bzero)
    B("calloc")
    B("clock")
    S("close", mcfm_darwin_close)
    S("closedir", mcfm_darwin_closedir)
    S("connect", mcfm_darwin_connect)
    B("cos")
    B("cosf")
    B("difftime")
    S("dispatch_async", mcfm_darwin_dispatch_async)
    S("dispatch_async_f", mcfm_darwin_dispatch_async_f)
    S("dispatch_get_global_queue", mcfm_darwin_dispatch_get_global_queue)
    S("dispatch_once", mcfm_darwin_dispatch_once)
    S("dispatch_queue_create", mcfm_darwin_dispatch_queue_create)
    S("dispatch_semaphore_create", mcfm_darwin_dispatch_semaphore_create)
    S("dispatch_semaphore_signal", mcfm_darwin_dispatch_semaphore_signal)
    S("dispatch_semaphore_wait", mcfm_darwin_dispatch_semaphore_wait)
    S("dispatch_sync", mcfm_darwin_dispatch_sync)
    S("dispatch_time", mcfm_darwin_dispatch_time)
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
    S("freeaddrinfo", mcfm_darwin_freeaddrinfo)
    S("freehostent", mcfm_darwin_freehostent)
    S("freeifaddrs", mcfm_darwin_freeifaddrs)
    B("freelocale")
    B("frexp")
    S("fscanf", mcfm_darwin_fscanf)
    B("fseek")
    B("fseeko")
    S("fstat", mcfm_darwin_fstat)
    B("fsync")
    B("ftell")
    B("ftello")
    B("fwrite")
    S("gai_strerror", mcfm_darwin_gai_strerror)
    S("getaddrinfo", mcfm_darwin_getaddrinfo)
    B("getenv")
    B("geteuid")
    S("gethostbyaddr", mcfm_darwin_gethostbyaddr)
    S("gethostbyname", mcfm_darwin_gethostbyname)
    B("gethostname")
    S("getifaddrs", mcfm_darwin_getifaddrs)
    S("getipnodebyname", mcfm_darwin_getipnodebyname)
    S("getnameinfo", mcfm_darwin_getnameinfo)
    S("getpagesize", mcfm_darwin_getpagesize)
    S("getpeername", mcfm_darwin_getpeername)
    B("getpid")
    B("getservbyname")
    S("getsockname", mcfm_darwin_getsockname)
    S("getsockopt", mcfm_darwin_getsockopt)
    B("gettimeofday")
    B("getuid")
    B("gmtime")
    B("gmtime_r")
    S("hash_create", mcfm_darwin_hash_create)
    S("hash_search", mcfm_darwin_hash_search)
    S("host_page_size", mcfm_darwin_host_page_size)
    S("host_statistics", mcfm_darwin_host_statistics)
    B("if_indextoname")
    B("if_nametoindex")
    B("in6addr_any")
    B("in6addr_loopback")
    B("inet_addr")
    B("inet_ntoa")
    S("inet_ntop", mcfm_darwin_inet_ntop)
    S("inet_pton", mcfm_darwin_inet_pton)
    S("ioctl", mcfm_darwin_ioctl)
    S("kevent", mcfm_darwin_kevent)
    S("kqueue", mcfm_darwin_kqueue)
    B("ldexp")
    B("listen")
    B("localtime")
    B("localtime_r")
    B("log")
    B("log10")
    B("log10f")
    B("logf")
    S("lstat", mcfm_darwin_lstat)
    S("mach_absolute_time", mcfm_darwin_mach_absolute_time)
    S("mach_host_self", mcfm_darwin_mach_host_self)
    S("mach_task_self_", &mcfm_darwin_mach_task_self_)
    S("mach_timebase_info", mcfm_darwin_mach_timebase_info)
    B("malloc")
    B("memchr")
    B("memcmp")
    B("memcpy")
    B("memmove")
    B("memset")
    S("memset_pattern16", mcfm_darwin_memset_pattern16)
    B("mkdir")
    B("mktime")
    S("mmap", mcfm_darwin_mmap)
    B("munmap")
    S("newlocale", mcfm_darwin_newlocale)
    S("open", mcfm_darwin_open)
    S("opendir", mcfm_darwin_opendir)
    S("perror", mcfm_darwin_perror)
    B("pipe")
    S("poll", mcfm_darwin_poll)
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
    S("raise", mcfm_darwin_raise)
    B("rand")
    B("read")
    S("readdir", mcfm_darwin_readdir)
    S("readdir_r", mcfm_darwin_readdir_r)
    B("realloc")
    S("recv", mcfm_darwin_recv)
    S("recvfrom", mcfm_darwin_recvfrom)
    S("recvmsg", mcfm_darwin_recvmsg)
    B("remove")
    B("rename")
    B("rmdir")
    B("sched_yield")
    B("select")
    S("semaphore_create", mcfm_darwin_semaphore_create)
    S("semaphore_destroy", mcfm_darwin_semaphore_destroy)
    S("semaphore_signal", mcfm_darwin_semaphore_signal)
    S("semaphore_wait", mcfm_darwin_semaphore_wait)
    S("send", mcfm_darwin_send)
    S("sendmsg", mcfm_darwin_sendmsg)
    S("sendto", mcfm_darwin_sendto)
    S("setlocale", mcfm_darwin_setlocale)
    S("setsockopt", mcfm_darwin_setsockopt)
    B("shutdown")
    S("sigaction", mcfm_darwin_sigaction)
    S("signal", mcfm_darwin_signal)
    B("sin")
    B("sinf")
    B("sleep")
    S("snprintf", mcfm_darwin_snprintf)
    S("socket", mcfm_darwin_socket)
    S("sprintf", mcfm_darwin_sprintf)
    B("srand")
    S("sscanf", mcfm_darwin_sscanf)
    S("stat", mcfm_darwin_stat)
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
    B("symlink")
    S("sysconf", mcfm_darwin_sysconf)
    S("sysctlbyname", mcfm_darwin_sysctlbyname)
    B("tan")
    B("tanf")
    S("tcgetattr", mcfm_darwin_tcgetattr)
    S("tcsetattr", mcfm_darwin_tcsetattr)
    B("time")
    B("timegm")
    B("umask")
    S("uname", mcfm_darwin_uname)
    B("unlink")
    B("uselocale")
    B("usleep")
    S("vfprintf", mcfm_darwin_vfprintf)
    S("vsnprintf", mcfm_darwin_vsnprintf)
    S("write", mcfm_darwin_write)
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
