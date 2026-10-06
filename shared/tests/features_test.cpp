#include <mcfm/keyboard_mouse.h>
#include <mcfm/win10_ui.h>
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
static int orig_platform_type(void *) { return 1; }
static bool orig_metadata_screens(void *) { return false; }
static void orig_noop(void *) {}

static int hides = 0, shows = 0;
static void on_hide() { hides++; }
static void on_show() { shows++; }

template <class F> static F slot_fn(FakePlatform &p, Slot s) { return (F)p.slot(s); }

static void pocket_vtable(FakePlatform &p) {
  p.set_slot(Slot::GetEdition, (void *)&orig_edition);
  p.set_slot(Slot::UIScalingRules, (void *)&orig_scaling);
  p.set_slot(Slot::UseCenteredGUI, (void *)&orig_centered);
  p.set_slot(Slot::DefaultInputMode, (void *)&orig_input_mode);
  p.set_slot(Slot::PlatformType, (void *)&orig_platform_type);
  p.set_slot(Slot::UseMetadataDrivenScreens, (void *)&orig_metadata_screens);
  p.set_slot(Slot::HideMousePointer, (void *)&orig_noop);
  p.set_slot(Slot::ShowMousePointer, (void *)&orig_noop);
}

static void test_win10_ui() {
  FakePlatform p;
  pocket_vtable(p);
  EXPECT(mcfm::win10_ui::install(p));
  typedef std::string (*EditionFn)(void *);
  typedef int (*IntFn)(void *);
  typedef bool (*BoolFn)(void *);
  EXPECT(slot_fn<EditionFn>(p, Slot::GetEdition)(0) == "win10");
  EXPECT(slot_fn<IntFn>(p, Slot::UIScalingRules)(0) == 0);
  EXPECT(slot_fn<BoolFn>(p, Slot::UseCenteredGUI)(0) == true);
  EXPECT(slot_fn<IntFn>(p, Slot::PlatformType)(0) == 0);
  EXPECT(slot_fn<BoolFn>(p, Slot::UseMetadataDrivenScreens)(0) == true);
  // input mode is not the Win10 UI's business
  EXPECT(slot_fn<IntFn>(p, Slot::DefaultInputMode)(0) == 2);
}

static void test_win10_ui_missing_slot() {
  FakePlatform p;
  p.set_slot(Slot::GetEdition, (void *)&orig_edition);
  p.set_slot(Slot::UIScalingRules, (void *)&orig_scaling);  // no UseCenteredGUI here
  EXPECT(mcfm::win10_ui::install(p));
  EXPECT(p.logged("UseCenteredGUI"));
  EXPECT(p.logged("PlatformType") && p.logged("UseMetadataDrivenScreens"));
  EXPECT(((std::string(*)(void *))p.slot(Slot::GetEdition))(0) == "win10");
  FakePlatform none;  // without GetEdition the module cannot work
  EXPECT(!mcfm::win10_ui::install(none));
}

static void test_keyboard_mouse_slots() {
  FakePlatform p;
  pocket_vtable(p);
  mcfm::keyboard_mouse::PointerCallbacks cb = {&on_hide, &on_show};
  EXPECT(mcfm::keyboard_mouse::install(p, cb));
  EXPECT(((int (*)(void *))p.slot(Slot::DefaultInputMode))(0) == 1);
  ((void (*)(void *))p.slot(Slot::HideMousePointer))(0);
  ((void (*)(void *))p.slot(Slot::ShowMousePointer))(0);
  ((void (*)(void *))p.slot(Slot::ShowMousePointer))(0);
  EXPECT(hides == 1 && shows == 2);
  FakePlatform noGlobals;
  pocket_vtable(noGlobals);
  noGlobals.hasGlobals = false;
  EXPECT(!mcfm::keyboard_mouse::install(noGlobals, cb));
}

static void test_keys_grow_engine_vector() {
  FakePlatform p;
  pocket_vtable(p);
  p.keyboardInputs.reserve(4);  // force growth past the engine's capacity
  p.keyboardInputs.push_back(mcfm::engine::KeyEvent());
  p.keyboardInputs[0].key = 99;
  mcfm::keyboard_mouse::PointerCallbacks cb = {&on_hide, &on_show};
  EXPECT(mcfm::keyboard_mouse::install(p, cb));
  for (int i = 0; i < 1000; i++) mcfm::keyboard_mouse::key('A' + i % 26, i % 2 == 0);
  EXPECT(p.keyboardInputs.size() == 1001);
  EXPECT(p.keyboardInputs.capacity() >= 1001);
  EXPECT(p.keyboardInputs[0].key == 99);  // existing entry preserved
  EXPECT(p.keyboardInputs[1].key == 'A' && p.keyboardInputs[1].state == 1);
  EXPECT(p.keyboardInputs[2].key == 'B' && p.keyboardInputs[2].state == 0);
  EXPECT(p.keyboardInputs[1000].key == 'A' + 999 % 26);
  EXPECT(p.keyboardStates['A' + 999 % 26] == 0);
  mcfm::keyboard_mouse::key('W', true);
  EXPECT(p.keyboardStates['W'] == 1);
  mcfm::keyboard_mouse::key(0, true);
  mcfm::keyboard_mouse::key(300, true);  // out of range: ignored
  EXPECT(p.keyboardInputs.size() == 1002);
  p.keyboardInputs.clear();  // the engine drains it; vector must still be valid
  mcfm::keyboard_mouse::key('E', true);
  EXPECT(p.keyboardInputs.size() == 1 && p.keyboardInputs[0].key == 'E');
}

static void test_mouse_calls() {
  FakePlatform p;
  pocket_vtable(p);
  mcfm::keyboard_mouse::PointerCallbacks cb = {&on_hide, &on_show};
  EXPECT(mcfm::keyboard_mouse::install(p, cb));
  mcfm::keyboard_mouse::mouse_button(mcfm::engine::mouse::Left, true, 10, 20);
  mcfm::keyboard_mouse::mouse_move_abs(30, 40);
  mcfm::keyboard_mouse::mouse_move_rel(5, -3);
  mcfm::keyboard_mouse::mouse_move_rel(0, 0);  // nothing to send
  mcfm::keyboard_mouse::mouse_wheel(1, 30, 40);
  mcfm::keyboard_mouse::mouse_wheel(-2, 30, 40);
  mcfm::keyboard_mouse::mouse_wheel(0, 30, 40);  // nothing to send
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
  std::printf("features_test: all passed\n");
  return 0;
}
