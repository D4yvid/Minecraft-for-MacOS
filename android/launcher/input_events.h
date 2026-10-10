#pragma once
// Input plumbing of the Android app (docs/LAUNCHER.md, Stage 3c), platform-free: the UI thread
// pushes events, the render thread drains them between frames and feeds the engine. Touches go to
// the engine's Multitouch::feed as iOS's touch handlers do. C++11.
#include <mutex>
#include <string>
#include <vector>

namespace mcfm {
namespace android {

enum class EventType { Touch, Key, MouseButton, MouseMove, MouseWheel, Text, Backspace, Return };

struct Event {
  EventType type = EventType::Key;
  int a = 0, b = 0;      // Touch: action, pointer id; Key: vk, down; MouseButton: button, down;
                         // MouseWheel: notches
  float x = 0, y = 0;    // pixels (MouseMove: relative when a != 0)
  std::string text;      // Text
};

class EventQueue {
 public:
  void push(const Event &e);
  void drain(std::vector<Event> *out);  // everything queued so far, in order; clears the queue

 private:
  std::mutex lock_;
  std::vector<Event> events_;
};

enum class TouchAction { Down = 0, Move = 1, Up = 2, Cancel = 3 };

// The game tracks 12 touches (slots 0-11); Android pointer ids are arbitrary small integers.
class TouchSlots {
 public:
  int down(int pointer);   // a free slot, or -1 when all are taken
  int find(int pointer) const;
  int up(int pointer);     // the slot released, or -1

 private:
  int pointers_[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// Multitouch::feed(button, state, x, y, slot): iOS feeds (1,1) on touch down, (0,0) on move and
// (1,0) on up/cancel, with x/y in pixels.
struct FeedCall { int button, state, x, y, slot; };

// false when the event does not reach the game (unknown pointer, no free slot).
bool touch_feed(TouchSlots *slots, TouchAction action, int pointer, float x, float y, FeedCall *out);

// An Android KeyEvent key code -> the Windows virtual key the engine's Keyboard uses (0: none).
// BACK is Escape (the game's "back" on desktop).
int android_key_to_vk(int keycode);

}  // namespace android
}  // namespace mcfm
