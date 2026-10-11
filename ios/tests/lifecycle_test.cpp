// The iOS engine's lifecycle (ios/launcher/lifecycle.h): suspend saves, resume comes before
// focus, nothing reaches a suspended engine but resume (a resize while suspended crashed on
// Android): the size is kept and applied after resume, before focus. Host test.
#include <cstdio>
#include <string>

#include "lifecycle.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FakeEngine {
  std::string calls;
  void focus_lost() { calls += "focus_lost "; }
  void focus_gained() { calls += "focus_gained "; }
  void suspend() { calls += "suspend "; }
  void resume() { calls += "resume "; }
  void resize(int w, int h) { calls += "resize " + std::to_string(w) + "x" + std::to_string(h) + " "; }
};

using mcfm::launcher::Lifecycle;

int main() {
  {  // nothing before the engine started; a pause that came first applies at start
    FakeEngine e;
    Lifecycle<FakeEngine> l(e);
    l.set_paused(true);
    l.resize(10, 20);
    EXPECT(e.calls.empty());
    EXPECT(!l.running());
    l.started();
    EXPECT(e.calls == "focus_lost suspend ");
    EXPECT(!l.running());
  }
  {  // running: a resize goes straight through
    FakeEngine e;
    Lifecycle<FakeEngine> l(e);
    l.started();
    EXPECT(l.running());
    l.resize(300, 200);
    EXPECT(e.calls == "resize 300x200 ");
  }
  {  // suspended: the size waits for resume, then comes before focus
    FakeEngine e;
    Lifecycle<FakeEngine> l(e);
    l.started();
    l.set_paused(true);
    e.calls.clear();
    l.resize(300, 200);
    l.resize(400, 200);
    EXPECT(e.calls.empty());
    l.set_paused(false);
    EXPECT(e.calls == "resume resize 400x200 focus_gained ");
    EXPECT(l.running());
    e.calls.clear();
    l.set_paused(true);
    l.set_paused(false);  // no size pending: none sent
    EXPECT(e.calls == "focus_lost suspend resume focus_gained ");
  }
  {  // pausing twice suspends once
    FakeEngine e;
    Lifecycle<FakeEngine> l(e);
    l.started();
    l.set_paused(true);
    l.set_paused(true);
    EXPECT(e.calls == "focus_lost suspend ");
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("lifecycle_test: all passed\n");
  return 0;
}
