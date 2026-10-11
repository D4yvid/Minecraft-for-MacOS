// The app's input plumbing (android/launcher/input_events.cpp, platform-free): events cross from
// the UI thread to the render thread in order; Android key codes map to Windows virtual keys.
// (Touch slots: shared/tests/touch_input_test.cpp.) Host test (make test).
#include <cstdio>
#include <thread>
#include <vector>

#include "input_events.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using namespace mcfm::android;

int main() {
  // The queue: events from another thread arrive complete and in order.
  EventQueue queue;
  std::thread producer([&queue] {
    for (int i = 0; i < 10000; i++) {
      Event e;
      e.type = EventType::Key;
      e.a = i;
      queue.push(e);
    }
    Event t;
    t.type = EventType::Text;
    t.text = "héllo";
    queue.push(t);
  });
  std::vector<Event> got;
  while (got.size() < 10001) {
    std::vector<Event> batch;
    queue.drain(&batch);
    got.insert(got.end(), batch.begin(), batch.end());
  }
  producer.join();
  bool ordered = true;
  for (int i = 0; i < 10000; i++) ordered &= got[i].type == EventType::Key && got[i].a == i;
  EXPECT(ordered);
  EXPECT(got[10000].type == EventType::Text && got[10000].text == "héllo");

  // Android key codes -> Windows virtual keys (the engine's Keyboard), as the macOS keymap does.
  EXPECT(android_key_to_vk(29) == 'A' && android_key_to_vk(54) == 'Z');   // KEYCODE_A, KEYCODE_Z
  EXPECT(android_key_to_vk(7) == '0' && android_key_to_vk(16) == '9');    // KEYCODE_0, KEYCODE_9
  EXPECT(android_key_to_vk(62) == 0x20 && android_key_to_vk(66) == 0x0D); // SPACE, ENTER
  EXPECT(android_key_to_vk(111) == 0x1B && android_key_to_vk(4) == 0x1B); // ESCAPE, BACK
  EXPECT(android_key_to_vk(59) == 0x10 && android_key_to_vk(113) == 0x11);// SHIFT_LEFT, CTRL_LEFT
  EXPECT(android_key_to_vk(19) == 0x26 && android_key_to_vk(22) == 0x27); // DPAD_UP, DPAD_RIGHT
  EXPECT(android_key_to_vk(131) == 0x70 && android_key_to_vk(142) == 0x7B); // F1, F12
  EXPECT(android_key_to_vk(67) == 0x08 && android_key_to_vk(61) == 0x09); // DEL (backspace), TAB
  EXPECT(android_key_to_vk(9999) == 0 && android_key_to_vk(-1) == 0);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("input_events_test: all passed\n");
  return 0;
}
