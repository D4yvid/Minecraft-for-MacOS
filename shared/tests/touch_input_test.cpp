// Touches (shared/launcher/touch_input.cpp, used by the Android and iOS apps): pointer ids map to
// the game's 12 touch slots and actions to the Multitouch::feed triples iOS's touch handlers use.
#include <cstdio>

#include "touch_input.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using namespace mcfm::launcher;

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

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("touch_input_test: all passed\n");
  return 0;
}
