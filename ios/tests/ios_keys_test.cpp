// Hardware keys and text boxes on iOS (ios/launcher/ios_keys.h): while a text box is open UIKit
// gets every press (its characters reach the text box through insertText / deleteBackward) and
// the engine only Escape; otherwise the engine gets them all. Only real transitions reach the
// engine, so a key held across the text box's opening or closing is released once; a release
// goes to UIKit when UIKit had the press. Host test.
#include <cstdio>

#include "ios_keys.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

using mcfm::launcher::IosKeys;
using mcfm::launcher::KeyRoute;

static const int kA = 0x04, kReturn = 0x28, kEscape = 0x29, kBackspace = 0x2A, kShift = 0xE1, kUnknown = 0x200;

int main() {
  {  // playing: every mapped key goes to the engine, none to UIKit (it would type into the game)
    IosKeys k;
    KeyRoute r = k.press(kA, true, false);
    EXPECT(r.vk == 'A' && r.down && !r.to_text);
    r = k.press(kA, false, false);
    EXPECT(r.vk == 'A' && !r.down && !r.to_text);
    r = k.press(kUnknown, true, false);
    EXPECT(r.vk == 0 && !r.to_text);
    r = k.press(kA, false, false);  // released twice: no second release
    EXPECT(r.vk == 0);
  }
  {  // typing: letters, Return and Backspace go to UIKit only (no double Enter)
    IosKeys k;
    KeyRoute r = k.press(kA, true, true);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kA, false, true);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kReturn, true, true);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kReturn, false, true);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kBackspace, true, true);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kShift, true, true);
    EXPECT(r.vk == 0 && r.to_text);
  }
  {  // typing: Escape still reaches the engine (it closes the chat), and UIKit
    IosKeys k;
    KeyRoute r = k.press(kEscape, true, true);
    EXPECT(r.vk == 0x1B && r.down && r.to_text);
    r = k.press(kEscape, false, true);
    EXPECT(r.vk == 0x1B && !r.down && r.to_text);
  }
  {  // a key held when the text box opens (T for chat) is released in the engine
    IosKeys k;
    EXPECT(k.press(kA, true, false).vk == 'A');
    KeyRoute r = k.press(kA, false, true);
    EXPECT(r.vk == 'A' && !r.down && r.to_text);
  }
  {  // the Return that closed the text box: its release is no stray key in the engine, and
     // UIKit, which had the press, gets the release (no key left down there)
    IosKeys k;
    k.press(kReturn, true, true);
    KeyRoute r = k.press(kReturn, false, false);
    EXPECT(r.vk == 0 && r.to_text);
    r = k.press(kReturn, true, false);  // the next press is the game's again
    EXPECT(r.vk == 0x0D && !r.to_text);
    r = k.press(kReturn, false, false);
    EXPECT(r.vk == 0x0D && !r.to_text);
  }
  {  // usages beyond the table still route by mode
    IosKeys k;
    EXPECT(k.press(kUnknown, true, true).to_text && k.press(kUnknown, false, true).to_text);
    EXPECT(!k.press(kUnknown, true, false).to_text);
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("ios_keys_test: all passed\n");
  return 0;
}
