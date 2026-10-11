#pragma once
// Touches for the engine's Multitouch::feed, as iOS's touch handlers feed them; used by the
// Android app and our iOS app (docs/LAUNCHER.md, Stages 3c and 4). Platform-free, C++11.

namespace mcfm {
namespace launcher {

enum class TouchAction { Down = 0, Move = 1, Up = 2, Cancel = 3 };

// The game tracks 12 touches (slots 0-11); pointer ids (Android pointer ids, iOS touch numbers)
// are arbitrary small integers.
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

}  // namespace launcher
}  // namespace mcfm
