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

// Logs "mcfm: <message>" once per call site key (the message itself).
void mcfm_darwin_log_once(const char *message);

}  // extern "C"
