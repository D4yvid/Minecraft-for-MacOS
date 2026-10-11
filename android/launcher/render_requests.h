#pragma once
// What the Android app's UI thread asks of the render thread (docs/LAUNCHER.md, Stage 3c) and
// when it waits: only for the guarantees Android needs (the window let go of before
// surfaceDestroyed returns, the game saved before onPause returns); a new window, a resume and
// focus changes never block the UI thread (the first load takes seconds). Platform-free, C++11.
#include <condition_variable>
#include <mutex>

namespace mcfm {
namespace android {

// Reference counting of the window handle (ANativeWindow_acquire/release in the app).
struct WindowRefs {
  void (*acquire)(void *window);
  void (*release)(void *window);
};

// The requests the render thread takes at once; it owns a reference to `window`.
struct Requests {
  bool window_changed = false;
  void *window = nullptr;
  int width = 0, height = 0;
  bool pause_changed = false, paused = false;
  bool focus_changed = false, focused = true;
  long ticket = 0;  // the requests up to this one are in here
};

class RenderRequests {
 public:
  explicit RenderRequests(WindowRefs refs);

  // UI thread. A null window waits until the render thread let go of the one it holds;
  // pausing waits until the running engine is suspended.
  void set_window(void *window, int width, int height);
  void set_paused(bool paused);
  void set_focus(bool focused);

  // Render thread: takes what was asked (blocks while there is nothing, unless `drawing`).
  Requests wait(bool drawing);
  void ack(const Requests &done);  // the requests in `done` are handled
  void set_engine_started();
  void finish();  // the render thread ended: nobody waits any more

 private:
  void request_and_wait(std::unique_lock<std::mutex> &hold, bool must_wait);

  WindowRefs refs_;
  std::mutex lock_;
  std::condition_variable changed_, acked_cv_;
  Requests pending_;
  long requested_ = 0, acked_ = 0;
  bool holds_window_ = false, engine_started_ = false, finished_ = false;
};

}  // namespace android
}  // namespace mcfm
