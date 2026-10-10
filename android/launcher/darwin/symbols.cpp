// The libSystem table (docs/LAUNCHER.md, Stage 3a): every libSystem symbol the image may import,
// by its name without the Mach-O leading underscore. Each one is either
//   SHIM:    our Darwin-behaviour implementation,
//   BIONIC:  bionic's function or variable of the same name (same ABI, asserted per area),
//   RUNTIME: this runtime's own export (libc++abi / libunwind, e.g. _Unwind_Resume).
// Anything else is missing: the load fails naming it. Sorted by name (tools/tests check it).
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "darwin.h"

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
    R("_Unwind_Resume")
    B("getpid")
    B("malloc")
    B("memcmp")
    B("memmove")
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
    B("sched_yield")
    B("strlen")
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
