// The UI thread → render thread handshake of the Android app (render_requests.h): what waits,
// what does not, and that an ack never covers a request the render thread has not taken.
#include "render_requests.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace mcfm::android;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::atomic<int> refs[4];
static void acquire(void *w) { refs[reinterpret_cast<intptr_t>(w)]++; }
static void release(void *w) { refs[reinterpret_cast<intptr_t>(w)]--; }
static void *win(int i) { return reinterpret_cast<void *>(static_cast<intptr_t>(i)); }

// Runs f on another thread; true when it has not returned after a while (it is waiting).
template <class F> static bool blocks(F f, std::thread *t, std::atomic<bool> *done) {
  *done = false;
  *t = std::thread([f, done] { f(); *done = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return !*done;
}

int main() {
  WindowRefs wr = {&acquire, &release};
  {  // A new window and a resume never wait; the render thread takes the window with a reference.
    RenderRequests r(wr);
    r.set_window(win(1), 640, 480);
    r.set_paused(false);
    EXPECT(refs[1] == 1);
    Requests q = r.wait(false);
    EXPECT(q.window_changed && q.window == win(1) && q.width == 640 && q.height == 480);
    EXPECT(q.pause_changed && !q.paused);
    r.ack(q);
    release(q.window);
    // Nothing held, engine not started: losing the window and pausing do not wait either.
    RenderRequests r2(wr);
    r2.set_window(win(2), 1, 1);
    r2.set_window(nullptr, 0, 0);  // the pending window is let go of at once
    EXPECT(refs[2] == 0);
    r2.set_paused(true);
    r2.finish();
  }
  {  // The thread holds a window: losing it waits for the ack of that very request.
    RenderRequests r(wr);
    r.set_window(win(1), 1, 1);
    Requests q = r.wait(false);
    r.ack(q);
    std::thread t;
    std::atomic<bool> done(false);
    EXPECT(blocks([&r] { r.set_window(nullptr, 0, 0); }, &t, &done));
    q.window_changed = false;
    r.ack(q);  // an ack of the earlier snapshot does not release it
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(!done);
    Requests q2 = r.wait(true);
    EXPECT(q2.window_changed && q2.window == nullptr);
    release(win(1));
    r.ack(q2);
    t.join();
    EXPECT(done);
  }
  {  // Engine running: a pause waits until the render thread suspended it, even when it arrives
     // while the thread is busy with an earlier request (the focus loss before onPause).
    RenderRequests r(wr);
    r.set_engine_started();
    r.set_focus(false);
    Requests busy = r.wait(false);
    EXPECT(busy.focus_changed && !busy.focused && !busy.pause_changed);
    std::thread t;
    std::atomic<bool> done(false);
    EXPECT(blocks([&r] { r.set_paused(true); }, &t, &done));
    r.ack(busy);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT(!done);
    Requests q = r.wait(true);
    EXPECT(q.pause_changed && q.paused);
    r.ack(q);
    t.join();
    EXPECT(done);
  }
  {  // The render thread ends: waiters return, a pending window is let go of.
    RenderRequests r(wr);
    r.set_engine_started();
    r.set_window(win(3), 1, 1);
    std::thread t;
    std::atomic<bool> done(false);
    EXPECT(blocks([&r] { r.set_paused(true); }, &t, &done));
    r.finish();
    t.join();
    EXPECT(done && refs[3] == 0);
    r.set_paused(false);  // after the end nothing waits
    r.set_window(win(3), 1, 1);  // not kept: nobody would release it
  }
  for (int i = 0; i < 4; i++) EXPECT(refs[i] == 0);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("render_requests_test: all passed\n");
  return 0;
}
