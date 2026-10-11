#include "touch_input.h"

namespace mcfm {
namespace launcher {

int TouchSlots::down(int pointer) {
  int existing = find(pointer);
  if (existing >= 0) return existing;  // a repeated down: keep its slot
  for (int i = 0; i < 12; i++)
    if (pointers_[i] < 0) {
      pointers_[i] = pointer;
      return i;
    }
  return -1;
}

int TouchSlots::find(int pointer) const {
  for (int i = 0; i < 12; i++)
    if (pointers_[i] == pointer) return i;
  return -1;
}

int TouchSlots::up(int pointer) {
  int slot = find(pointer);
  if (slot >= 0) pointers_[slot] = -1;
  return slot;
}

bool touch_feed(TouchSlots *slots, TouchAction action, int pointer, float x, float y, FeedCall *out) {
  int slot = action == TouchAction::Down ? slots->down(pointer)
             : action == TouchAction::Move ? slots->find(pointer)
                                           : slots->up(pointer);
  if (slot < 0) return false;
  out->button = action == TouchAction::Move ? 0 : 1;
  out->state = action == TouchAction::Down ? 1 : 0;
  out->x = x > 0 ? static_cast<int>(x) : 0;
  out->y = y > 0 ? static_cast<int>(y) : 0;
  out->slot = slot;
  return true;
}

}  // namespace launcher
}  // namespace mcfm
