// Text queue protocol (as iOS ShowKeyboardView) and the keyboard slot wrappers.
#include "app_platform.h"
#include "text_input.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int base_show = 0, base_hide = 0, cb_show = 0, cb_hide = 0;
static std::string shown_text;
static void base_show_fn(void *, const std::string &, int, bool, bool, const void *) { base_show++; }
static void base_hide_fn(void *) { base_hide++; }

int main() {
  std::vector<TextEvent> q;
  uintptr_t qa = reinterpret_cast<uintptr_t>(&q);
  push_text(qa, "a");
  push_text(qa, "\xC3\xA9\xF0\x9F\x98\x80z");  // é, 😀, z
  push_backspace(qa);
  push_return(qa);
  EXPECT(q.size() == 6);
  if (q.size() == 6) {
    EXPECT(q[0].text == "a" && !q[0].newline);
    EXPECT(q[1].text == "\xC3\xA9" && q[2].text == "\xF0\x9F\x98\x80" && q[3].text == "z");
    EXPECT(q[4].text == "\b" && !q[4].newline);
    EXPECT(q[5].text == "\n" && q[5].newline);
  }
  for (int i = 0; i < 1000; i++) push_text(qa, "x");  // grows like the engine's vector
  EXPECT(q.size() == 1006 && q.back().text == "x");
  push_text(qa, std::string("\xFF", 1));  // invalid byte: its own element
  EXPECT(q.back().text == std::string("\xFF", 1));

  void *base[kBaseSlots] = {}, *vt[kBaseSlots];
  base[9] = reinterpret_cast<void *>(&base_show_fn);
  base[10] = reinterpret_cast<void *>(&base_hide_fn);
  EngineFns fns = {nullptr, nullptr, nullptr, nullptr};
  build_vtable(vt, base, fns);
  auto show = reinterpret_cast<void (*)(void *, const std::string &, int, bool, bool, const void *)>(vt[9]);
  auto hide = reinterpret_cast<void (*)(void *)>(vt[10]);
  hide(nullptr);  // before any callbacks: base only, no crash
  EXPECT(base_hide == 1);
  KeyboardCallbacks cb = {[](const std::string &t) { cb_show++; shown_text = t; }, [] { cb_hide++; }};
  set_keyboard_callbacks(cb);
  float pos[2] = {0, 0};
  show(nullptr, "hello", 64, false, false, pos);
  EXPECT(base_show == 1 && cb_show == 1 && shown_text == "hello");
  hide(nullptr);
  EXPECT(base_hide == 2 && cb_hide == 1);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_text_test: all passed\n");
  return 0;
}
