#pragma once
// The iOS engine's lifecycle (docs/LAUNCHER.md, Stage 4): the scene leaving the screen suspends
// the engine (the game saves), coming back resumes it, and resume comes before focus (as on
// Android). A suspended engine hears nothing but resume (on Android a resize or focus_gained
// while suspended crashed): a new drawable size is kept and applied after resume, before focus.
// E: the engine (focus_lost, suspend, resume, focus_gained, resize). Platform-free, C++11.

#include <cstdio>

namespace mcfm {
namespace launcher {

template <class E> class Lifecycle {
 public:
  explicit Lifecycle(E &engine) : e_(engine) {}

  void started() {  // the engine runs: a pause that came before applies now
    started_ = true;
    sync();
  }
  void set_paused(bool paused) {
    paused_ = paused;
    sync();
  }
  void resize(int w, int h) {
    if (!started_) return;  // the engine starts at the drawable's size
    if (suspended_) {
      pending_ = true;
      w_ = w;
      h_ = h;
      return;
    }
    e_.resize(w, h);
  }
  bool running() const { return started_ && !suspended_; }  // frames and input may reach it

 private:
  void sync() {
    if (!started_) return;
    if (paused_ && !suspended_) {
      e_.focus_lost();
      e_.suspend();
      suspended_ = true;
      std::fprintf(stderr, "mcfm: paused (saved)\n");
    } else if (!paused_ && suspended_) {
      e_.resume();
      suspended_ = false;
      if (pending_) {
        pending_ = false;
        e_.resize(w_, h_);
      }
      e_.focus_gained();
    }
  }

  E &e_;
  bool started_ = false, paused_ = false, suspended_ = false, pending_ = false;
  int w_ = 0, h_ = 0;
};

}  // namespace launcher
}  // namespace mcfm
