// Darwin pthread objects on bionic. A Darwin mutex/cond is {sig, impl}: `impl` points to a bionic
// object allocated on first use. Darwin's static initializers only set `sig`, so the first user
// converts the object, exactly once: CAS sig INIT -> BUSY, allocate, publish impl, sig = READY;
// threads that lose the race wait for READY. Once objects run our own state machine; attributes
// keep their fields inside the Darwin bytes. Return values are Darwin errno numbers.
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "darwin.h"

namespace {

// Signatures of objects we initialized (Darwin's own values for initialized objects).
constexpr long kMutexReady = 0x4D555458;   // 'MUTX'
constexpr long kCondReady = 0x434F4E44;    // 'COND'
constexpr long kBusy = 0x6D63666D;         // 'mcfm': being converted by another thread
constexpr long kMutexAttrSig = 0x4D545841; // 'MTXA'
constexpr long kCondAttrSig = 0x434E4441;  // 'CNDA'
constexpr long kAttrSig = 0x54484441;      // 'THDA'

std::atomic<long> &sig_of(long *sig) { return *reinterpret_cast<std::atomic<long> *>(sig); }

template <typename D> void *&impl_of(D *object) { return *reinterpret_cast<void **>(object->opaque); }

int ret(int bionic_result) { return mcfm_darwin_errno(bionic_result); }

int bionic_mutex_type(int darwin_type) {
  if (darwin_type == darwin::kPTHREAD_MUTEX_RECURSIVE) return PTHREAD_MUTEX_RECURSIVE;
  if (darwin_type == darwin::kPTHREAD_MUTEX_ERRORCHECK) return PTHREAD_MUTEX_ERRORCHECK;
  return PTHREAD_MUTEX_NORMAL;
}

pthread_mutex_t *new_mutex(int darwin_type) {
  pthread_mutex_t *m = static_cast<pthread_mutex_t *>(malloc(sizeof(pthread_mutex_t)));
  if (!m) return nullptr;
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, bionic_mutex_type(darwin_type));
  pthread_mutex_init(m, &a);
  pthread_mutexattr_destroy(&a);
  return m;
}

int static_mutex_type(long sig) {
  if (sig == darwin::k_PTHREAD_MUTEX_SIG_init || sig == darwin::k_PTHREAD_FIRSTFIT_MUTEX_SIG_init)
    return darwin::kPTHREAD_MUTEX_NORMAL;
  if (sig == darwin::k_PTHREAD_RECURSIVE_MUTEX_SIG_init) return darwin::kPTHREAD_MUTEX_RECURSIVE;
  if (sig == darwin::k_PTHREAD_ERRORCHECK_MUTEX_SIG_init) return darwin::kPTHREAD_MUTEX_ERRORCHECK;
  return -1;
}

// Returns the bionic object behind `object`, converting a statically initialized one once.
// `ready`: our signature; `make(sig)`: allocates the bionic object for a static signature, or
// nullptr when `sig` is not one.
template <typename D, typename Make> void *resolve(D *object, long ready, Make make) {
  std::atomic<long> &sig = sig_of(&object->sig);
  for (;;) {
    long s = sig.load(std::memory_order_acquire);
    if (s == ready) return impl_of(object);
    if (s == kBusy) { sched_yield(); continue; }
    void *created = make(s);
    if (!created) return nullptr;  // not a valid object
    if (sig.compare_exchange_strong(s, kBusy, std::memory_order_acq_rel)) {
      impl_of(object) = created;
      sig.store(ready, std::memory_order_release);
      return created;
    }
    free(created);  // another thread converts it
  }
}

pthread_mutex_t *mutex(darwin::pthread_mutex *m) {
  return static_cast<pthread_mutex_t *>(resolve(m, kMutexReady, [](long s) -> void * {
    int type = static_mutex_type(s);
    return type < 0 ? nullptr : new_mutex(type);
  }));
}

pthread_cond_t *cond(darwin::pthread_cond *c) {
  return static_cast<pthread_cond_t *>(resolve(c, kCondReady, [](long s) -> void * {
    if (s != darwin::k_PTHREAD_COND_SIG_init) return nullptr;
    pthread_cond_t *b = static_cast<pthread_cond_t *>(malloc(sizeof(pthread_cond_t)));
    if (b) pthread_cond_init(b, nullptr);
    return b;
  }));
}

// pthread_attr fields kept inside the Darwin object.
struct Attr { int detach; int policy; int priority; size_t stacksize; };
static_assert(sizeof(Attr) <= sizeof(darwin::pthread_attr::opaque), "Attr");
Attr *attr_of(darwin::pthread_attr *a) { return reinterpret_cast<Attr *>(a->opaque); }
const Attr *attr_of(const darwin::pthread_attr *a) { return reinterpret_cast<const Attr *>(a->opaque); }

int &mutexattr_type(darwin::pthread_mutexattr *a) { return *reinterpret_cast<int *>(a->opaque); }

// pthread_once: 0 not run, 1 running, 2 done (in the Darwin object's opaque bytes).
pthread_mutex_t g_once_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_once_done = PTHREAD_COND_INITIALIZER;

}  // namespace

extern "C" {

int mcfm_darwin_pthread_mutex_init(darwin::pthread_mutex *m, const darwin::pthread_mutexattr *attr) {
  int type = attr && attr->sig == kMutexAttrSig ? *reinterpret_cast<const int *>(attr->opaque)
                                                 : darwin::kPTHREAD_MUTEX_DEFAULT;
  pthread_mutex_t *b = new_mutex(type);
  if (!b) return darwin::kENOMEM;
  impl_of(m) = b;
  sig_of(&m->sig).store(kMutexReady, std::memory_order_release);
  return 0;
}

int mcfm_darwin_pthread_mutex_lock(darwin::pthread_mutex *m) {
  pthread_mutex_t *b = mutex(m);
  return b ? ret(pthread_mutex_lock(b)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_mutex_trylock(darwin::pthread_mutex *m) {
  pthread_mutex_t *b = mutex(m);
  return b ? ret(pthread_mutex_trylock(b)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_mutex_unlock(darwin::pthread_mutex *m) {
  pthread_mutex_t *b = mutex(m);
  return b ? ret(pthread_mutex_unlock(b)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_mutex_destroy(darwin::pthread_mutex *m) {
  long s = sig_of(&m->sig).load();
  if (s == kMutexReady) {
    pthread_mutex_t *b = static_cast<pthread_mutex_t *>(impl_of(m));
    int r = pthread_mutex_destroy(b);
    if (r) return ret(r);
    free(b);
  } else if (static_mutex_type(s) < 0) {
    return darwin::kEINVAL;
  }
  sig_of(&m->sig).store(0);
  return 0;
}

int mcfm_darwin_pthread_mutexattr_init(darwin::pthread_mutexattr *a) {
  a->sig = kMutexAttrSig;
  mutexattr_type(a) = darwin::kPTHREAD_MUTEX_DEFAULT;
  return 0;
}

int mcfm_darwin_pthread_mutexattr_settype(darwin::pthread_mutexattr *a, int type) {
  if (a->sig != kMutexAttrSig) return darwin::kEINVAL;
  if (type != darwin::kPTHREAD_MUTEX_NORMAL && type != darwin::kPTHREAD_MUTEX_RECURSIVE &&
      type != darwin::kPTHREAD_MUTEX_ERRORCHECK)
    return darwin::kEINVAL;
  mutexattr_type(a) = type;
  return 0;
}

int mcfm_darwin_pthread_mutexattr_destroy(darwin::pthread_mutexattr *a) {
  if (a->sig != kMutexAttrSig) return darwin::kEINVAL;
  a->sig = 0;
  return 0;
}

int mcfm_darwin_pthread_cond_init(darwin::pthread_cond *c, const darwin::pthread_condattr *) {
  pthread_cond_t *b = static_cast<pthread_cond_t *>(malloc(sizeof(pthread_cond_t)));
  if (!b) return darwin::kENOMEM;
  pthread_cond_init(b, nullptr);  // process-private, CLOCK_REALTIME, as Darwin's
  impl_of(c) = b;
  sig_of(&c->sig).store(kCondReady, std::memory_order_release);
  return 0;
}

int mcfm_darwin_pthread_cond_wait(darwin::pthread_cond *c, darwin::pthread_mutex *m) {
  pthread_cond_t *bc = cond(c);
  pthread_mutex_t *bm = mutex(m);
  return bc && bm ? ret(pthread_cond_wait(bc, bm)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_cond_timedwait(darwin::pthread_cond *c, darwin::pthread_mutex *m, const struct timespec *abstime) {
  pthread_cond_t *bc = cond(c);
  pthread_mutex_t *bm = mutex(m);
  return bc && bm ? ret(pthread_cond_timedwait(bc, bm, abstime)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_cond_signal(darwin::pthread_cond *c) {
  pthread_cond_t *b = cond(c);
  return b ? ret(pthread_cond_signal(b)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_cond_broadcast(darwin::pthread_cond *c) {
  pthread_cond_t *b = cond(c);
  return b ? ret(pthread_cond_broadcast(b)) : darwin::kEINVAL;
}

int mcfm_darwin_pthread_cond_destroy(darwin::pthread_cond *c) {
  long s = sig_of(&c->sig).load();
  if (s == kCondReady) {
    pthread_cond_t *b = static_cast<pthread_cond_t *>(impl_of(c));
    int r = pthread_cond_destroy(b);
    if (r) return ret(r);
    free(b);
  } else if (s != darwin::k_PTHREAD_COND_SIG_init) {
    return darwin::kEINVAL;
  }
  sig_of(&c->sig).store(0);
  return 0;
}

int mcfm_darwin_pthread_condattr_init(darwin::pthread_condattr *a) {
  a->sig = kCondAttrSig;
  return 0;
}

int mcfm_darwin_pthread_condattr_destroy(darwin::pthread_condattr *a) {
  if (a->sig != kCondAttrSig) return darwin::kEINVAL;
  a->sig = 0;
  return 0;
}

int mcfm_darwin_pthread_once(darwin::pthread_once *o, void (*routine)(void)) {
  std::atomic<int> &state = *reinterpret_cast<std::atomic<int> *>(o->opaque);
  if (state.load(std::memory_order_acquire) == 2) return 0;
  int expected = 0;
  if (state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
    try {
      routine();
    } catch (...) {  // Darwin: a routine that throws leaves the once object unrun
      pthread_mutex_lock(&g_once_lock);
      state.store(0, std::memory_order_release);
      pthread_cond_broadcast(&g_once_done);
      pthread_mutex_unlock(&g_once_lock);
      throw;
    }
    pthread_mutex_lock(&g_once_lock);
    state.store(2, std::memory_order_release);
    pthread_cond_broadcast(&g_once_done);
    pthread_mutex_unlock(&g_once_lock);
    return 0;
  }
  pthread_mutex_lock(&g_once_lock);
  while (state.load(std::memory_order_acquire) == 1) pthread_cond_wait(&g_once_done, &g_once_lock);
  pthread_mutex_unlock(&g_once_lock);
  // The routine threw in another thread: run it here, as Darwin would let a later caller.
  return state.load(std::memory_order_acquire) == 2 ? 0 : mcfm_darwin_pthread_once(o, routine);
}

int mcfm_darwin_pthread_key_create(darwin::pthread_key *key, void (*destructor)(void *)) {
  pthread_key_t k;
  int r = pthread_key_create(&k, destructor);
  if (r) return ret(r);
  *key = static_cast<darwin::pthread_key>(static_cast<unsigned int>(k));
  return 0;
}

int mcfm_darwin_pthread_key_delete(darwin::pthread_key key) {
  return ret(pthread_key_delete(static_cast<pthread_key_t>(key)));
}

void *mcfm_darwin_pthread_getspecific(darwin::pthread_key key) {
  return pthread_getspecific(static_cast<pthread_key_t>(key));
}

int mcfm_darwin_pthread_setspecific(darwin::pthread_key key, const void *value) {
  return ret(pthread_setspecific(static_cast<pthread_key_t>(key), value));
}

int mcfm_darwin_pthread_attr_init(darwin::pthread_attr *a) {
  a->sig = kAttrSig;
  Attr *f = attr_of(a);
  f->detach = darwin::kPTHREAD_CREATE_JOINABLE;
  f->policy = darwin::kSCHED_OTHER;
  f->priority = 31;            // Darwin's default priority
  f->stacksize = 512 * 1024;   // Darwin's default for secondary threads
  return 0;
}

int mcfm_darwin_pthread_attr_destroy(darwin::pthread_attr *a) {
  if (a->sig != kAttrSig) return darwin::kEINVAL;
  a->sig = 0;
  return 0;
}

int mcfm_darwin_pthread_attr_setdetachstate(darwin::pthread_attr *a, int state) {
  if (a->sig != kAttrSig) return darwin::kEINVAL;
  if (state != darwin::kPTHREAD_CREATE_JOINABLE && state != darwin::kPTHREAD_CREATE_DETACHED) return darwin::kEINVAL;
  attr_of(a)->detach = state;
  return 0;
}

int mcfm_darwin_pthread_attr_setstacksize(darwin::pthread_attr *a, size_t size) {
  if (a->sig != kAttrSig || size < static_cast<size_t>(darwin::kPTHREAD_STACK_MIN)) return darwin::kEINVAL;
  attr_of(a)->stacksize = size;
  return 0;
}

int mcfm_darwin_pthread_attr_setschedpolicy(darwin::pthread_attr *a, int policy) {
  if (a->sig != kAttrSig) return darwin::kEINVAL;
  if (policy != darwin::kSCHED_OTHER && policy != darwin::kSCHED_FIFO && policy != darwin::kSCHED_RR) return darwin::kEINVAL;
  attr_of(a)->policy = policy;
  return 0;
}

int mcfm_darwin_pthread_attr_setschedparam(darwin::pthread_attr *a, const darwin::sched_param *param) {
  if (a->sig != kAttrSig) return darwin::kEINVAL;
  attr_of(a)->priority = param->sched_priority;
  return 0;
}

int mcfm_darwin_pthread_create(darwin::pthread_t *thread, const darwin::pthread_attr *attr, void *(*start)(void *), void *arg) {
  pthread_attr_t b;
  pthread_attr_init(&b);
  if (attr) {
    if (attr->sig != kAttrSig) { pthread_attr_destroy(&b); return darwin::kEINVAL; }
    const Attr *f = attr_of(attr);
    pthread_attr_setdetachstate(&b, f->detach == darwin::kPTHREAD_CREATE_DETACHED ? PTHREAD_CREATE_DETACHED
                                                                                   : PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&b, f->stacksize);
    // Real-time policies need privileges an app does not have: threads keep the default
    // policy (Darwin's priorities are hints too).
    if (f->policy != darwin::kSCHED_OTHER) mcfm_darwin_log_once("pthread: real-time scheduling ignored");
  }
  pthread_t t;
  int r = pthread_create(&t, &b, start, arg);
  pthread_attr_destroy(&b);
  if (r) return ret(r);
  *thread = static_cast<darwin::pthread_t>(t);
  return 0;
}

int mcfm_darwin_pthread_join(darwin::pthread_t thread, void **result) {
  return ret(pthread_join(static_cast<pthread_t>(thread), result));
}

int mcfm_darwin_pthread_detach(darwin::pthread_t thread) {
  return ret(pthread_detach(static_cast<pthread_t>(thread)));
}

darwin::pthread_t mcfm_darwin_pthread_self(void) { return static_cast<darwin::pthread_t>(pthread_self()); }

int mcfm_darwin_pthread_setname_np(const char *name) {
  char truncated[16];  // the kernel keeps 15 characters
  strncpy(truncated, name, sizeof truncated - 1);
  truncated[sizeof truncated - 1] = 0;
  return ret(pthread_setname_np(pthread_self(), truncated));
}

}  // extern "C"
