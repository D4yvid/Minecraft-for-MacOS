#include "input_events.h"

namespace mcfm {
namespace android {

void EventQueue::push(const Event &e) {
  std::lock_guard<std::mutex> hold(lock_);
  events_.push_back(e);
}

void EventQueue::drain(std::vector<Event> *out) {
  out->clear();
  std::lock_guard<std::mutex> hold(lock_);
  out->swap(events_);
}

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

int android_key_to_vk(int k) {
  // android/keycodes.h (AKEYCODE_*): letters 29-54, digits 7-16, F1-F12 131-142.
  if (k >= 29 && k <= 54) return 'A' + (k - 29);
  if (k >= 7 && k <= 16) return '0' + (k - 7);
  if (k >= 131 && k <= 142) return 0x70 + (k - 131);
  if (k >= 144 && k <= 153) return 0x60 + (k - 144);  // numpad 0-9
  switch (k) {
    case 4: return 0x1B;    // BACK -> Escape
    case 111: return 0x1B;  // ESCAPE
    case 66: return 0x0D;   // ENTER
    case 160: return 0x0D;  // NUMPAD_ENTER
    case 62: return 0x20;   // SPACE
    case 61: return 0x09;   // TAB
    case 67: return 0x08;   // DEL (backspace)
    case 112: return 0x2E;  // FORWARD_DEL
    case 59: return 0x10;   // SHIFT_LEFT
    case 60: return 0x10;   // SHIFT_RIGHT
    case 113: return 0x11;  // CTRL_LEFT
    case 114: return 0x11;  // CTRL_RIGHT
    case 57: return 0x12;   // ALT_LEFT
    case 58: return 0x12;   // ALT_RIGHT
    case 19: return 0x26;   // DPAD_UP
    case 20: return 0x28;   // DPAD_DOWN
    case 21: return 0x25;   // DPAD_LEFT
    case 22: return 0x27;   // DPAD_RIGHT
    case 92: return 0x21;   // PAGE_UP
    case 93: return 0x22;   // PAGE_DOWN
    case 122: return 0x24;  // MOVE_HOME
    case 123: return 0x23;  // MOVE_END
    case 124: return 0x2D;  // INSERT
    case 115: return 0x14;  // CAPS_LOCK
    case 55: return 0xBC;   // COMMA
    case 56: return 0xBE;   // PERIOD
    case 69: return 0xBD;   // MINUS
    case 70: return 0xBB;   // EQUALS
    case 71: return 0xDB;   // LEFT_BRACKET
    case 72: return 0xDD;   // RIGHT_BRACKET
    case 73: return 0xDC;   // BACKSLASH
    case 74: return 0xBA;   // SEMICOLON
    case 75: return 0xDE;   // APOSTROPHE
    case 76: return 0xBF;   // SLASH
    case 68: return 0xC0;   // GRAVE
    default: return 0;
  }
}

}  // namespace android
}  // namespace mcfm
