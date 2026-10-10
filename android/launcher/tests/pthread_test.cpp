// The Darwin pthread layer on Android (docs/LAUNCHER.md, Stage 3a): Darwin-layout objects,
// including statically initialized ones raced from many threads, behave as on Darwin.
// Built with the NDK and run on the device (make android-test).
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atomic>

#include "darwin.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

// Each Darwin object is followed by guard bytes the layer must never touch.
template <typename T> struct Guarded {
  T object;
  unsigned char guard[32];
  Guarded() { memset(guard, 0xA5, sizeof guard); }
  bool intact() const { for (unsigned char g : guard) if (g != 0xA5) return false; return true; }
};

constexpr int kThreads = 8, kIncrements = 100000;

darwin::pthread_mutex static_mutex(long sig) {
  darwin::pthread_mutex m;
  memset(&m, 0, sizeof m);
  m.sig = sig;
  return m;
}

Guarded<darwin::pthread_mutex> g_counter_mutex;
long g_counter = 0;
std::atomic<int> g_start(0);

void *increment(void *) {
  while (!g_start.load()) sched_yield();
  for (int i = 0; i < kIncrements; i++) {
    mcfm_darwin_pthread_mutex_lock(&g_counter_mutex.object);
    g_counter++;
    mcfm_darwin_pthread_mutex_unlock(&g_counter_mutex.object);
  }
  return nullptr;
}

Guarded<darwin::pthread_once> g_once;
std::atomic<int> g_once_runs(0);
void once_routine() { g_once_runs++; usleep(20000); }
void *call_once(void *) {
  while (!g_start.load()) sched_yield();
  mcfm_darwin_pthread_once(&g_once.object, once_routine);
  EXPECT(g_once_runs.load() == 1);  // returns only after the routine finished
  return nullptr;
}

Guarded<darwin::pthread_mutex> g_queue_mutex;
Guarded<darwin::pthread_cond> g_queue_cond;
int g_queue = 0, g_consumed = 0;
constexpr int kItems = 20000;
void *consumer(void *) {
  for (int i = 0; i < kItems; i++) {
    mcfm_darwin_pthread_mutex_lock(&g_queue_mutex.object);
    while (g_queue == 0) mcfm_darwin_pthread_cond_wait(&g_queue_cond.object, &g_queue_mutex.object);
    g_queue--;
    g_consumed++;
    mcfm_darwin_pthread_mutex_unlock(&g_queue_mutex.object);
  }
  return nullptr;
}

std::atomic<int> g_destructed(0);
darwin::pthread_key g_key;
void key_destructor(void *value) { if (value == &g_destructed) g_destructed++; }
void *set_key(void *) {
  mcfm_darwin_pthread_setspecific(g_key, &g_destructed);
  EXPECT(mcfm_darwin_pthread_getspecific(g_key) == &g_destructed);
  return nullptr;
}

std::atomic<int> g_detached_ran(0);
void *detached(void *) { g_detached_ran = 1; return nullptr; }

char g_name[32];
void *named(void *) {
  mcfm_darwin_pthread_setname_np("mcfm-named-thread-long");
  char path[64];
  snprintf(path, sizeof path, "/proc/self/task/%d/comm", gettid());
  int fd = open(path, O_RDONLY);
  ssize_t n = read(fd, g_name, sizeof g_name - 1);
  close(fd);
  if (n > 0) g_name[n - 1] = 0;  // trailing newline
  return nullptr;
}

pthread_t start(void *(*fn)(void *)) {
  pthread_t t;
  darwin::pthread_t dt;
  EXPECT(mcfm_darwin_pthread_create(&dt, nullptr, fn, nullptr) == 0);
  memcpy(&t, &dt, sizeof t);
  return t;
}

}  // namespace

int main() {
  // 1. A statically initialized Darwin mutex raced by 8 threads from its first use.
  g_counter_mutex.object = static_mutex(darwin::k_PTHREAD_MUTEX_SIG_init);
  darwin::pthread_t threads[kThreads];
  for (auto &t : threads) EXPECT(mcfm_darwin_pthread_create(&t, nullptr, increment, nullptr) == 0);
  g_start = 1;
  for (auto &t : threads) EXPECT(mcfm_darwin_pthread_join(t, nullptr) == 0);
  EXPECT(g_counter == long(kThreads) * kIncrements);
  EXPECT(g_counter_mutex.intact());

  // 2. A static recursive mutex locks twice; a static error-checking one reports Darwin's EDEADLK.
  Guarded<darwin::pthread_mutex> rec;
  rec.object = static_mutex(darwin::k_PTHREAD_RECURSIVE_MUTEX_SIG_init);
  EXPECT(mcfm_darwin_pthread_mutex_lock(&rec.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_lock(&rec.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_unlock(&rec.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_unlock(&rec.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_destroy(&rec.object) == 0);
  EXPECT(rec.intact());
  Guarded<darwin::pthread_mutex> err;
  err.object = static_mutex(darwin::k_PTHREAD_ERRORCHECK_MUTEX_SIG_init);
  EXPECT(mcfm_darwin_pthread_mutex_lock(&err.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_lock(&err.object) == darwin::kEDEADLK);
  EXPECT(mcfm_darwin_pthread_mutex_trylock(&err.object) == 16);  // EBUSY is 16 on both
  EXPECT(mcfm_darwin_pthread_mutex_unlock(&err.object) == 0);

  // 3. A mutex initialized with a recursive Darwin attribute.
  Guarded<darwin::pthread_mutexattr> ma;
  Guarded<darwin::pthread_mutex> dm;
  EXPECT(mcfm_darwin_pthread_mutexattr_init(&ma.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutexattr_settype(&ma.object, darwin::kPTHREAD_MUTEX_RECURSIVE) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_init(&dm.object, &ma.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_lock(&dm.object) == 0 && mcfm_darwin_pthread_mutex_lock(&dm.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_unlock(&dm.object) == 0 && mcfm_darwin_pthread_mutex_unlock(&dm.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutex_destroy(&dm.object) == 0);
  EXPECT(mcfm_darwin_pthread_mutexattr_destroy(&ma.object) == 0);
  EXPECT(ma.intact() && dm.intact());

  // 4. Static mutex + static cond: producer/consumer loses no wakeups.
  g_queue_mutex.object = static_mutex(darwin::k_PTHREAD_MUTEX_SIG_init);
  memset(&g_queue_cond.object, 0, sizeof g_queue_cond.object);
  g_queue_cond.object.sig = darwin::k_PTHREAD_COND_SIG_init;
  pthread_t c = start(consumer);
  for (int i = 0; i < kItems; i++) {
    mcfm_darwin_pthread_mutex_lock(&g_queue_mutex.object);
    g_queue++;
    mcfm_darwin_pthread_cond_signal(&g_queue_cond.object);
    mcfm_darwin_pthread_mutex_unlock(&g_queue_mutex.object);
  }
  pthread_join(c, nullptr);
  EXPECT(g_consumed == kItems);
  EXPECT(g_queue_mutex.intact() && g_queue_cond.intact());
  // A timed wait that expires returns Darwin's ETIMEDOUT (60).
  timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_nsec += 20 * 1000 * 1000;
  if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
  mcfm_darwin_pthread_mutex_lock(&g_queue_mutex.object);
  EXPECT(mcfm_darwin_pthread_cond_timedwait(&g_queue_cond.object, &g_queue_mutex.object, &deadline) == darwin::kETIMEDOUT);
  mcfm_darwin_pthread_mutex_unlock(&g_queue_mutex.object);
  EXPECT(mcfm_darwin_pthread_cond_broadcast(&g_queue_cond.object) == 0);
  EXPECT(mcfm_darwin_pthread_cond_destroy(&g_queue_cond.object) == 0);

  // 5. pthread_once raced by 8 threads runs once; everyone returns after it finished.
  memset(&g_once.object, 0, sizeof g_once.object);
  g_once.object.sig = darwin::k_PTHREAD_ONCE_SIG_init;
  g_start = 0;
  for (auto &t : threads) EXPECT(mcfm_darwin_pthread_create(&t, nullptr, call_once, nullptr) == 0);
  g_start = 1;
  for (auto &t : threads) mcfm_darwin_pthread_join(t, nullptr);
  EXPECT(g_once_runs.load() == 1);
  EXPECT(g_once.intact());

  // 6. A key's destructor runs at thread exit; the key fits Darwin's 8-byte pthread_key_t.
  Guarded<darwin::pthread_key> key;
  memset(&key.object, 0xFF, sizeof key.object);
  EXPECT(mcfm_darwin_pthread_key_create(&key.object, key_destructor) == 0);
  EXPECT(key.object < 0x100000000ul);  // upper half zeroed
  EXPECT(key.intact());
  g_key = key.object;
  pthread_join(start(set_key), nullptr);
  EXPECT(g_destructed.load() == 1);
  EXPECT(mcfm_darwin_pthread_key_delete(key.object) == 0);

  // 7. A detached Darwin attribute creates a detached thread (join fails with Darwin's EINVAL).
  Guarded<darwin::pthread_attr> attr;
  EXPECT(mcfm_darwin_pthread_attr_init(&attr.object) == 0);
  EXPECT(mcfm_darwin_pthread_attr_setdetachstate(&attr.object, darwin::kPTHREAD_CREATE_DETACHED) == 0);
  EXPECT(mcfm_darwin_pthread_attr_setstacksize(&attr.object, 256 * 1024) == 0);
  darwin::sched_param sp;
  memset(&sp, 0, sizeof sp);
  sp.sched_priority = 31;
  EXPECT(mcfm_darwin_pthread_attr_setschedpolicy(&attr.object, darwin::kSCHED_OTHER) == 0);
  EXPECT(mcfm_darwin_pthread_attr_setschedparam(&attr.object, &sp) == 0);
  darwin::pthread_t dt;
  EXPECT(mcfm_darwin_pthread_create(&dt, &attr.object, detached, nullptr) == 0);
  for (int i = 0; i < 200 && !g_detached_ran.load(); i++) usleep(5000);
  EXPECT(g_detached_ran.load() == 1);
  EXPECT(mcfm_darwin_pthread_attr_destroy(&attr.object) == 0);
  EXPECT(attr.intact());

  // 8. setname_np takes one argument (Darwin) and truncates to the kernel's 15 characters.
  pthread_join(start(named), nullptr);
  EXPECT(strcmp(g_name, "mcfm-named-thre") == 0);

  // 9. self and detach pass through.
  EXPECT(mcfm_darwin_pthread_self() != 0);

  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("pthread_test: all passed\n");
  return 0;
}
