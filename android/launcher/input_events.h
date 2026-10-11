#pragma once
// Input plumbing of the Android app (docs/LAUNCHER.md, Stage 3c), platform-free: the UI thread
// pushes events, the render thread drains them between frames and feeds the engine. Touches go to
// the engine's Multitouch::feed as iOS's touch handlers do. C++11.
#include <mutex>
#include <string>
#include <vector>

#include "touch_input.h"

namespace mcfm {
namespace android {

enum class EventType { Touch, Key, MouseButton, MouseMove, MouseWheel, Text, Backspace, Return, ImagePicked };

struct Event {
  EventType type = EventType::Key;
  int a = 0, b = 0;      // Touch: action, pointer id; Key: vk, down; MouseButton: button, down;
                         // MouseWheel: notches
  float x = 0, y = 0;    // pixels (MouseMove: relative when a != 0)
  std::string text;      // Text; ImagePicked: the PNG path (empty: cancelled)
};

class EventQueue {
 public:
  void push(const Event &e);
  void drain(std::vector<Event> *out);  // everything queued so far, in order; clears the queue

 private:
  std::mutex lock_;
  std::vector<Event> events_;
};

// Touches: shared with the iOS app (shared/launcher/touch_input.h).
using launcher::TouchAction;
using launcher::TouchSlots;
using launcher::FeedCall;
using launcher::touch_feed;

// An Android KeyEvent key code -> the Windows virtual key the engine's Keyboard uses (0: none).
// BACK is Escape (the game's "back" on desktop).
int android_key_to_vk(int keycode);

}  // namespace android
}  // namespace mcfm
