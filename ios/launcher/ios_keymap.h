#pragma once
// Hardware keyboards in the iOS app (docs/LAUNCHER.md, Stage 4): a UIKeyboardHIDUsage (USB HID
// usage page 7, UIKey.keyCode) -> the Windows virtual key the engine's Keyboard uses (0: none).
// Platform-free, C++11.

namespace mcfm {
namespace launcher {

int ios_hid_to_vk(int usage);

}  // namespace launcher
}  // namespace mcfm
