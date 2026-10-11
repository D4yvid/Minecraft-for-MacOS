#pragma once
// Hardware keys in the iOS app (docs/LAUNCHER.md, Stage 4): where a key press goes. While the
// game has a text box open, UIKit gets every press (UIKit types into a UIKeyInput responder only
// when the press reaches super: insertText / deleteBackward then feed the text box, Return
// included, which presses Enter once) and the engine only the keys that pass while typing
// (Escape), as the macOS launcher. Otherwise the engine gets them and UIKit none (it would type
// into the game). Releases reach the engine only for keys it holds, and UIKit whenever it had
// the press (Return that closes the text box: UIKit must not keep it down). Platform-free, C++11.

#include <mcfm/input/input_state.h>

#include "ios_keymap.h"

namespace mcfm {
namespace launcher {

struct KeyRoute {
  int vk;        // to the engine (0: nothing)
  bool down;
  bool to_text;  // UIKit gets the press too (super.pressesBegan/Ended)
};

class IosKeys {
 public:
  KeyRoute press(int hid_usage, bool down, bool text_mode) {
    int vk = ios_hid_to_vk(hid_usage);
    bool known = hid_usage >= 0 && hid_usage < kUsages;
    KeyRoute r = {0, down, text_mode};
    if (known) {
      if (!down && text_held_[hid_usage]) r.to_text = true;
      text_held_[hid_usage] = down && text_mode;
    }
    if (text_mode && down && !passes_while_typing(vk)) return r;  // the press belongs to the text box
    if (held_.set(vk, down)) r.vk = vk;
    return r;
  }

 private:
  static const int kUsages = 256;
  HeldSet held_;                       // by VK, as the engine has them
  bool text_held_[kUsages] = {};       // by HID usage, down in UIKit
};

}  // namespace launcher
}  // namespace mcfm
