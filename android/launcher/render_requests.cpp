#include "render_requests.h"

namespace mcfm {
namespace android {

RenderRequests::RenderRequests(WindowRefs refs) : refs_(refs) {}

void RenderRequests::set_window(void *window, int width, int height) {
  std::unique_lock<std::mutex> hold(lock_);
  if (finished_) return;  // nobody would take it
  if (pending_.window_changed && pending_.window) refs_.release(pending_.window);  // never taken
  if (window) refs_.acquire(window);
  pending_.window_changed = true;
  pending_.window = window;
  pending_.width = width;
  pending_.height = height;
  request_and_wait(hold, !window && holds_window_);
}

void RenderRequests::set_paused(bool paused) {
  std::unique_lock<std::mutex> hold(lock_);
  pending_.pause_changed = true;
  pending_.paused = paused;
  request_and_wait(hold, paused && engine_started_);
}

void RenderRequests::set_focus(bool focused) {
  std::unique_lock<std::mutex> hold(lock_);
  pending_.focus_changed = true;
  pending_.focused = focused;
  request_and_wait(hold, false);
}

// Called with lock_ held.
void RenderRequests::request_and_wait(std::unique_lock<std::mutex> &hold, bool must_wait) {
  long ticket = ++requested_;
  changed_.notify_all();
  if (must_wait) acked_cv_.wait(hold, [&] { return acked_ >= ticket || finished_; });
}

Requests RenderRequests::wait(bool drawing) {
  std::unique_lock<std::mutex> hold(lock_);
  changed_.wait(hold, [&] {
    return drawing || pending_.window_changed || pending_.pause_changed || pending_.focus_changed;
  });
  Requests taken = pending_;
  taken.ticket = requested_;
  if (taken.window_changed) holds_window_ = taken.window != nullptr;
  pending_.window_changed = pending_.pause_changed = pending_.focus_changed = false;
  pending_.window = nullptr;  // the reference went to the render thread
  return taken;
}

void RenderRequests::ack(const Requests &done) {
  std::lock_guard<std::mutex> hold(lock_);
  if (done.ticket > acked_) acked_ = done.ticket;
  acked_cv_.notify_all();
}

void RenderRequests::set_engine_started() {
  std::lock_guard<std::mutex> hold(lock_);
  engine_started_ = true;
}

void RenderRequests::finish() {
  std::lock_guard<std::mutex> hold(lock_);
  finished_ = true;
  if (pending_.window_changed && pending_.window) refs_.release(pending_.window);
  pending_.window_changed = false;
  pending_.window = nullptr;
  acked_cv_.notify_all();
}

}  // namespace android
}  // namespace mcfm
