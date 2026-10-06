#include <mcfm/modules/keyboard_mouse.h>
#include <mcfm/modules/win10_ui.h>
#include "fake_platform.h"

#include <cstdio>
#include <string>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using mcfm::engine::Slot;

// The "engine's own" AppPlatform virtuals (pocket values).
static std::string orig_edition(void *) { return "pocket"; }
static int orig_scaling(void *) { return 2; }
static bool orig_centered(void *) { return false; }
static int orig_input_mode(void *) { return 2; }
static void orig_noop(void *) {}

static bool win10 = true;
static bool win10_enabled() { return win10; }
static int hides = 0, shows = 0;
static void on_hide() { hides++; }
static void on_show() { shows++; }

template <class F> static F slot_fn(FakePlatform &p, Slot s) { return (F)p.slot(s); }

static void pocket_vtable(FakePlatform &p) {
  p.set_slot(Slot::GetEdition, (void *)&orig_edition);
  p.set_slot(Slot::UIScalingRules, (void *)&orig_scaling);
  p.set_slot(Slot::UseCenteredGUI, (void *)&orig_centered);
  p.set_slot(Slot::DefaultInputMode, (void *)&orig_input_mode);
  p.set_slot(Slot::HideMousePointer, (void *)&orig_noop);
  p.set_slot(Slot::ShowMousePointer, (void *)&orig_noop);
}

static void test_win10_ui() {
  FakePlatform p;
  pocket_vtable(p);
  mcfm::Win10UiModule m(&win10_enabled);
  EXPECT(m.init(p));
  typedef std::string (*EditionFn)(void *);
  typedef int (*IntFn)(void *);
  typedef bool (*BoolFn)(void *);
  win10 = true;
  EXPECT(slot_fn<EditionFn>(p, Slot::GetEdition)(0) == "win10");
  EXPECT(slot_fn<IntFn>(p, Slot::UIScalingRules)(0) == 0);
  EXPECT(slot_fn<BoolFn>(p, Slot::UseCenteredGUI)(0) == true);
  // switched off: the platform's own values come back (call-through to the originals)
  win10 = false;
  EXPECT(slot_fn<EditionFn>(p, Slot::GetEdition)(0) == "pocket");
  EXPECT(slot_fn<IntFn>(p, Slot::UIScalingRules)(0) == 2);
  EXPECT(slot_fn<BoolFn>(p, Slot::UseCenteredGUI)(0) == false);
  // input mode is not Win10Ui's business
  EXPECT(slot_fn<IntFn>(p, Slot::DefaultInputMode)(0) == 2);
  win10 = true;
}

static void test_win10_ui_missing_slot() {
  FakePlatform p;
  p.set_slot(Slot::GetEdition, (void *)&orig_edition);
  p.set_slot(Slot::UIScalingRules, (void *)&orig_scaling);  // no UseCenteredGUI here
  mcfm::Win10UiModule m(&win10_enabled);
  EXPECT(m.init(p));
  EXPECT(p.logged("UseCenteredGUI"));
  EXPECT(((std::string(*)(void *))p.slot(Slot::GetEdition))(0) == "win10");
  FakePlatform none;  // without GetEdition the module cannot work
  mcfm::Win10UiModule m2(&win10_enabled);
  EXPECT(!m2.init(none));
}

static void test_keyboard_mouse_slots() {
  FakePlatform p;
  pocket_vtable(p);
  mcfm::PointerCallbacks cb = {&on_hide, &on_show};
  mcfm::KeyboardMouseModule km(cb);
  EXPECT(km.init(p));
  EXPECT(((int (*)(void *))p.slot(Slot::DefaultInputMode))(0) == 1);
  ((void (*)(void *))p.slot(Slot::HideMousePointer))(0);
  ((void (*)(void *))p.slot(Slot::ShowMousePointer))(0);
  ((void (*)(void *))p.slot(Slot::ShowMousePointer))(0);
  EXPECT(hides == 1 && shows == 2);
  FakePlatform noGlobals;
  pocket_vtable(noGlobals);
  noGlobals.hasGlobals = false;
  mcfm::KeyboardMouseModule km2(cb);
  EXPECT(!km2.init(noGlobals));
}

static void test_keys_grow_engine_vector() {
  FakePlatform p;
  pocket_vtable(p);
  p.keyboardInputs.reserve(4);  // force growth past the engine's capacity
  p.keyboardInputs.push_back(mcfm::engine::KeyEvent());
  p.keyboardInputs[0].key = 99;
  mcfm::PointerCallbacks cb = {&on_hide, &on_show};
  mcfm::KeyboardMouseModule km(cb);
  EXPECT(km.init(p));
  for (int i = 0; i < 1000; i++) km.key('A' + i % 26, i % 2 == 0);
  EXPECT(p.keyboardInputs.size() == 1001);
  EXPECT(p.keyboardInputs.capacity() >= 1001);
  EXPECT(p.keyboardInputs[0].key == 99);  // existing entry preserved
  EXPECT(p.keyboardInputs[1].key == 'A' && p.keyboardInputs[1].state == 1);
  EXPECT(p.keyboardInputs[2].key == 'B' && p.keyboardInputs[2].state == 0);
  EXPECT(p.keyboardInputs[1000].key == 'A' + 999 % 26);
  EXPECT(p.keyboardStates['A' + 999 % 26] == 0);
  km.key('W', true);
  EXPECT(p.keyboardStates['W'] == 1);
  km.key(0, true);
  km.key(300, true);  // out of range: ignored
  EXPECT(p.keyboardInputs.size() == 1002);
  p.keyboardInputs.clear();  // the engine drains it; vector must still be valid
  km.key('E', true);
  EXPECT(p.keyboardInputs.size() == 1 && p.keyboardInputs[0].key == 'E');
}

static void test_mouse_calls() {
  FakePlatform p;
  pocket_vtable(p);
  mcfm::PointerCallbacks cb = {&on_hide, &on_show};
  mcfm::KeyboardMouseModule km(cb);
  EXPECT(km.init(p));
  km.mouse_button(mcfm::engine::mouse::Left, true, 10, 20);
  km.mouse_move_abs(30, 40);
  km.mouse_move_rel(5, -3);
  km.mouse_move_rel(0, 0);  // nothing to send
  km.mouse_wheel(1, 30, 40);
  km.mouse_wheel(-2, 30, 40);
  km.mouse_wheel(0, 30, 40);  // nothing to send
  EXPECT(p.mouse.size() == 5);
  MouseCall c0 = {1, 1, 10, 20, 0, 0}, c1 = {0, 0, 30, 40, 0, 0}, c2 = {0, 0, 0, 0, 5, -3},
            c3 = {4, 127, 30, 40, 0, 0}, c4 = {4, -127, 30, 40, 0, 0};
  MouseCall want[5] = {c0, c1, c2, c3, c4};
  for (int i = 0; i < 5 && i < (int)p.mouse.size(); i++) {
    const MouseCall &g = p.mouse[i], &w = want[i];
    EXPECT(g.btn == w.btn && g.state == w.state && g.x == w.x && g.y == w.y && g.dx == w.dx && g.dy == w.dy);
  }
}

int main() {
  test_win10_ui();
  test_win10_ui_missing_slot();
  test_keyboard_mouse_slots();
  test_keys_grow_engine_vector();
  test_mouse_calls();
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("modules_test: all passed\n");
  return 0;
}
