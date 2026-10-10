// A small libdispatch for the game: global (concurrent) queues backed by one thread pool, serial
// queues with one worker thread each, the main queue drained by the host
// (mcfm_darwin_drain_main_queue), dispatch_once, semaphores and dispatch_time. Blocks are copied
// when queued and released after running. dispatch_sync runs the work on the queue's thread
// and waits (Darwin runs it on the caller's thread; nothing in the game depends on which).
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

#include "darwin.h"

extern "C" {
void *mcfm_darwin_Block_copy(const void *);
void mcfm_darwin_Block_release(const void *);
uint64_t mcfm_darwin_mach_absolute_time(void);
}

namespace {

constexpr uint64_t kTimeNow = 0, kTimeForever = ~0ull;
constexpr long kTimedOut = 49;  // KERN_OPERATION_TIMED_OUT, what dispatch_semaphore_wait returns
static_assert(darwin::kKERN_OPERATION_TIMED_OUT == kTimedOut, "timed out");

struct BlockHeader {  // the Block ABI's leading fields
  void *isa;
  int32_t flags, reserved;
  void (*invoke)(void *);
};

struct Work {
  void (*function)(void *);
  void *context;
  bool block;  // context is a copied block to release after running
};

void run(const Work &w) {
  w.function(w.context);
  if (w.block) mcfm_darwin_Block_release(w.context);
}

void invoke_block(void *block) { static_cast<BlockHeader *>(block)->invoke(block); }

Work block_work(void *block) { return Work{invoke_block, mcfm_darwin_Block_copy(block), true}; }


}  // namespace

// A queue: serial (one worker, created on first use), concurrent (handed to the pool) or the
// main queue (drained by the host loop). Global name: symbols.cpp exports the main queue's address.
enum class mcfm_darwin_queue_kind { Serial, Concurrent, Main };
struct mcfm_darwin_queue {
  mcfm_darwin_queue_kind kind;
  std::mutex lock;
  std::condition_variable ready;
  std::deque<Work> items;
  bool worker_started = false;
  explicit mcfm_darwin_queue(mcfm_darwin_queue_kind k) : kind(k) {}
};

namespace {

typedef mcfm_darwin_queue Queue;
typedef mcfm_darwin_queue_kind Kind;

// The pool behind every concurrent queue.
struct Pool {
  std::mutex lock;
  std::condition_variable ready;
  std::deque<Work> items;
  bool started = false;
} g_pool;

void *pool_worker(void *) {
  pthread_setname_np(pthread_self(), "mcfm-dispatch");
  for (;;) {
    Work w;
    {
      std::unique_lock<std::mutex> hold(g_pool.lock);
      g_pool.ready.wait(hold, [] { return !g_pool.items.empty(); });
      w = g_pool.items.front();
      g_pool.items.pop_front();
    }
    run(w);
  }
  return nullptr;
}

void start_thread(void *(*fn)(void *), void *arg) {
  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&t, &attr, fn, arg);
  pthread_attr_destroy(&attr);
}

void *serial_worker(void *arg) {
  Queue *q = static_cast<Queue *>(arg);
  pthread_setname_np(pthread_self(), "mcfm-queue");
  for (;;) {
    Work w;
    {
      std::unique_lock<std::mutex> hold(q->lock);
      q->ready.wait(hold, [q] { return !q->items.empty(); });
      w = q->items.front();
      q->items.pop_front();
    }
    run(w);
  }
  return nullptr;
}

void enqueue(Queue *q, const Work &w) {
  if (q->kind == Kind::Concurrent) {
    std::lock_guard<std::mutex> hold(g_pool.lock);
    if (!g_pool.started) {
      g_pool.started = true;
      long n = sysconf(_SC_NPROCESSORS_ONLN);
      for (long i = 0; i < (n > 1 ? n : 2); i++) start_thread(pool_worker, nullptr);
    }
    g_pool.items.push_back(w);
    g_pool.ready.notify_one();
    return;
  }
  std::lock_guard<std::mutex> hold(q->lock);
  if (q->kind == Kind::Serial && !q->worker_started) {
    q->worker_started = true;
    start_thread(serial_worker, q);
  }
  q->items.push_back(w);
  q->ready.notify_one();
}

Queue g_global[4] = {Queue(Kind::Concurrent), Queue(Kind::Concurrent), Queue(Kind::Concurrent), Queue(Kind::Concurrent)};

struct Semaphore {
  std::mutex lock;
  std::condition_variable ready;
  long value;
  long waiters = 0;
};

// dispatch_time_t: nanoseconds of mach_absolute_time (our timebase is 1/1), or NOW/FOREVER.
bool wait_until(std::condition_variable &cv, std::unique_lock<std::mutex> &hold, uint64_t when,
                const std::function<bool()> &done) {
  if (when == kTimeForever) {
    cv.wait(hold, done);
    return true;
  }
  while (!done()) {
    uint64_t now = mcfm_darwin_mach_absolute_time();
    if (when == kTimeNow || now >= when) return false;
    cv.wait_for(hold, std::chrono::nanoseconds(when - now));
  }
  return true;
}

struct SyncState {
  std::mutex lock;
  std::condition_variable done_cv;
  bool done = false;
  Work work;
};

void run_sync(void *arg) {
  SyncState *s = static_cast<SyncState *>(arg);
  s->work.function(s->work.context);
  std::lock_guard<std::mutex> hold(s->lock);
  s->done = true;
  s->done_cv.notify_one();
}

}  // namespace

extern "C" {

// &_dispatch_main_q is dispatch_get_main_queue(); the game only uses its address.
Queue mcfm_darwin_dispatch_main_q(Kind::Main);

void *mcfm_darwin_dispatch_get_global_queue(long priority, unsigned long) {
  // DISPATCH_QUEUE_PRIORITY_HIGH 2, DEFAULT 0, LOW -2, BACKGROUND INT16_MIN
  int index = priority >= 2 ? 0 : priority >= 0 ? 1 : priority >= -2 ? 2 : 3;
  return &g_global[index];
}

void *mcfm_darwin_dispatch_queue_create(const char *, const void *attr) {
  return new Queue(attr ? Kind::Concurrent : Kind::Serial);  // attr: DISPATCH_QUEUE_CONCURRENT
}

void mcfm_darwin_dispatch_async(void *queue, void *block) { enqueue(static_cast<Queue *>(queue), block_work(block)); }

void mcfm_darwin_dispatch_async_f(void *queue, void *context, void (*function)(void *)) {
  enqueue(static_cast<Queue *>(queue), Work{function, context, false});
}

void mcfm_darwin_dispatch_sync(void *queue, void *block) {
  Queue *q = static_cast<Queue *>(queue);
  if (q->kind == Kind::Concurrent) {  // no ordering to respect: run here, as Darwin does
    invoke_block(block);
    return;
  }
  SyncState s;
  s.work = Work{invoke_block, block, false};  // the caller's block outlives the call: no copy
  enqueue(q, Work{run_sync, &s, false});
  std::unique_lock<std::mutex> hold(s.lock);
  s.done_cv.wait(hold, [&s] { return s.done; });
}

// The predicate: 0 not run, 1 running, ~0l done (the value Darwin's inline fast path checks).
void mcfm_darwin_dispatch_once(long *predicate, void *block) {
  std::atomic<long> &state = *reinterpret_cast<std::atomic<long> *>(predicate);
  long expected = 0;
  if (state.compare_exchange_strong(expected, 1)) {
    invoke_block(block);
    state.store(~0l, std::memory_order_release);
    return;
  }
  while (state.load(std::memory_order_acquire) != ~0l) sched_yield();
}

void *mcfm_darwin_dispatch_semaphore_create(long value) {
  if (value < 0) return nullptr;
  Semaphore *s = new Semaphore;
  s->value = value;
  return s;
}

long mcfm_darwin_dispatch_semaphore_signal(void *semaphore) {
  Semaphore *s = static_cast<Semaphore *>(semaphore);
  std::lock_guard<std::mutex> hold(s->lock);
  s->value++;
  s->ready.notify_one();
  return s->waiters > 0 ? 1 : 0;  // non-zero when a waiter is woken
}

long mcfm_darwin_dispatch_semaphore_wait(void *semaphore, uint64_t timeout) {
  Semaphore *s = static_cast<Semaphore *>(semaphore);
  std::unique_lock<std::mutex> hold(s->lock);
  s->waiters++;
  bool ok = wait_until(s->ready, hold, timeout, [s] { return s->value > 0; });
  s->waiters--;
  if (!ok) return kTimedOut;
  s->value--;
  return 0;
}

uint64_t mcfm_darwin_dispatch_time(uint64_t when, int64_t delta) {
  if (when == kTimeForever) return kTimeForever;
  uint64_t base = when == kTimeNow ? mcfm_darwin_mach_absolute_time() : when;
  if (delta < 0 && static_cast<uint64_t>(-delta) > base) return 1;
  uint64_t t = base + static_cast<uint64_t>(delta);
  return t == kTimeForever ? kTimeForever - 1 : t;
}

// The host's loop (mcfm-run; the platform layer in 3b) runs the main queue's work here.
void mcfm_darwin_drain_main_queue(void) {
  Queue *q = &mcfm_darwin_dispatch_main_q;
  for (;;) {
    Work w;
    {
      std::lock_guard<std::mutex> hold(q->lock);
      if (q->items.empty()) return;
      w = q->items.front();
      q->items.pop_front();
    }
    run(w);
  }
}

}  // extern "C"
