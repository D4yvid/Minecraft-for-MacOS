#pragma once
// macOS virtual key codes (Carbon kVK_*) -> Windows VK codes, as the engine expects.

namespace mcfm {
namespace launcher {

int mac_keycode_to_vk(unsigned short keycode);  // 0 for keys the game has no use for

}  // namespace launcher
}  // namespace mcfm
