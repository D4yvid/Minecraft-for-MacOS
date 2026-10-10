// LauncherPlatform: keyboard_mouse installs through our vtable and feeds fake engine globals.
#include "launcher_platform.h"

#include <mcfm/keyboard_mouse.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "engine_mouse.h"

using namespace mcfm::launcher;
namespace kbm = mcfm::keyboard_mouse;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FeedCall { void *dev; int btn, state, x, y; };
static std::vector<FeedCall> feeds;
static void fake_feed(void *dev, int btn, int state, int x, int y) { feeds.push_back({dev, btn, state, x, y}); }
static int grows = 0;
template <class T> struct RawVector { T *begin, *end, *cap; };
static void fake_grow(void *vec, mcfm::apple::MouseAction *a) {
  grows++;
  auto *v = static_cast<RawVector<mcfm::apple::MouseAction> *>(vec);
  static mcfm::apple::MouseAction big[64];
  size_t n = v->end - v->begin;
  std::memcpy(big, v->begin, n * sizeof *a);
  big[n] = *a;
  v->begin = big; v->end = big + n + 1; v->cap = big + 64;
}
static int hidden = 0, shown = 0;

int main() {
  void *vt[101] = {};
  RawVector<mcfm::engine::KeyEvent> keys = {nullptr, nullptr, nullptr};
  int32_t states[256] = {};
  mcfm::apple::MouseAction one[1];
  RawVector<mcfm::apple::MouseAction> mouse = {one, one, one + 1};
  char device;
  InputAddresses a;
  a.keyboard_inputs = reinterpret_cast<uintptr_t>(&keys);
  a.keyboard_states = reinterpret_cast<uintptr_t>(states);
  a.keyboard_text = 0;
  a.mouse_device = reinterpret_cast<uintptr_t>(&device);
  a.mouse_inputs = reinterpret_cast<uintptr_t>(&mouse);
  a.mouse_inputs_grow = reinterpret_cast<uintptr_t>(&fake_grow);
  a.mouse_device_feed = reinterpret_cast<uintptr_t>(&fake_feed);
  LauncherPlatform p(vt, a);
  kbm::PointerCallbacks cb = {[] { hidden++; }, [] { shown++; }};
  EXPECT(kbm::install(p, cb));
  EXPECT(vt[96] != nullptr && reinterpret_cast<int (*)(void *)>(vt[96])(nullptr) == 1);  // mouse input mode
  reinterpret_cast<void (*)(void *)>(vt[13])(nullptr);
  reinterpret_cast<void (*)(void *)>(vt[14])(nullptr);
  EXPECT(hidden == 1 && shown == 1);
  kbm::key(87, true);  // W
  EXPECT(keys.end - keys.begin == 1 && keys.begin[0].key == 87 && keys.begin[0].state == 1 && states[87] == 1);
  kbm::mouse_button(1, true, 10, 20);
  EXPECT(feeds.size() == 1 && feeds[0].dev == &device && feeds[0].btn == 1 && feeds[0].state == 1 && feeds[0].x == 10 && feeds[0].y == 20);
  kbm::mouse_move_rel(5, -3);
  kbm::mouse_move_rel(70000, 0);  // clamped, and the queue grows through the engine's function
  EXPECT(mouse.end - mouse.begin == 2 && mouse.begin[0].dx == 5 && mouse.begin[0].dy == -3 && mouse.begin[1].dx == 32767);
  EXPECT(grows == 1);
  void *orig = nullptr;
  EXPECT(p.patch_slot(mcfm::engine::Slot::GetEdition, reinterpret_cast<void *>(&fake_feed), &orig) && vt[93] == reinterpret_cast<void *>(&fake_feed));
  EXPECT(p.global(mcfm::engine::Global::KeyboardStates) == states);
  ::operator delete(keys.begin);  // grown by keyboard_mouse with ::operator new
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_platform_test: all passed\n");
  return 0;
}
