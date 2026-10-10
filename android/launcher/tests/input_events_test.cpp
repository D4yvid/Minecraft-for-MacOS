// The app's input plumbing (android/launcher/input_events.cpp, platform-free): Android pointer
// ids map to the game's 12 touch slots, actions to the iOS Multitouch::feed triples, and events
// cross from the UI thread to the render thread in order. Host test (make test).
#include <cstdio>
#include <thread>
#include <vector>

#include "input_events.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using namespace mcfm::android;

int main() {
  TouchSlots slots;
  FeedCall f;
  // Down / move / up: iOS's (1,1) (0,0) (1,0), pixels truncated like iOS's (int)(scale * pt).
  EXPECT(touch_feed(&slots, TouchAction::Down, 57, 100.7f, 200.2f, &f) && f.button == 1 && f.state == 1 && f.x == 100 &&
         f.y == 200 && f.slot == 0);
  EXPECT(touch_feed(&slots, TouchAction::Move, 57, 110.f, 210.f, &f) && f.button == 0 && f.state == 0 && f.slot == 0);
  // A second finger takes the next slot; moving it keeps it.
  EXPECT(touch_feed(&slots, TouchAction::Down, 3, 5.f, 6.f, &f) && f.slot == 1);
  EXPECT(touch_feed(&slots, TouchAction::Move, 3, 7.f, 8.f, &f) && f.slot == 1);
  EXPECT(touch_feed(&slots, TouchAction::Up, 57, 1.f, 2.f, &f) && f.button == 1 && f.state == 0 && f.slot == 0);
  // The freed slot is reused by the next finger.
  EXPECT(touch_feed(&slots, TouchAction::Down, 99, 1.f, 1.f, &f) && f.slot == 0);
  // Cancel releases like up.
  EXPECT(touch_feed(&slots, TouchAction::Cancel, 3, 1.f, 1.f, &f) && f.button == 1 && f.state == 0 && f.slot == 1);
  // A move or up for an unknown pointer is dropped.
  EXPECT(!touch_feed(&slots, TouchAction::Move, 12345, 1.f, 1.f, &f));
  EXPECT(!touch_feed(&slots, TouchAction::Up, 12345, 1.f, 1.f, &f));
  // At most 12 fingers: the 13th is dropped until one lifts.
  TouchSlots full;
  for (int i = 0; i < 12; i++) EXPECT(touch_feed(&full, TouchAction::Down, 100 + i, 0, 0, &f) && f.slot == i);
  EXPECT(!touch_feed(&full, TouchAction::Down, 200, 0, 0, &f));
  EXPECT(touch_feed(&full, TouchAction::Up, 105, 0, 0, &f) && f.slot == 5);
  EXPECT(touch_feed(&full, TouchAction::Down, 200, 0, 0, &f) && f.slot == 5);
  // Negative coordinates (a finger sliding off the edge) are clamped to 0.
  EXPECT(touch_feed(&full, TouchAction::Move, 200, -4.f, -1.f, &f) && f.x == 0 && f.y == 0);

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
