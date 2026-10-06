#pragma once

namespace mcpekbm {

// True when `header` is a mapped/loaded thin arm64 Mach-O whose LC_UUID is the one
// minecraftpe2 0.15.10 build this mod was reverse-engineered from. Reads only the
// header and load commands, so it is safe on any loaded image.
bool is_expected_game_image(const void *header);

}  // namespace mcpekbm
