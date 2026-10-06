#pragma once
#include <cstdint>

namespace eng {

bool init();
uintptr_t at(uintptr_t unslid);
bool patch_slot(uintptr_t vtableUnslid, uintptr_t byteOffset, void *fn);

void key(int vk, bool down);
void mouse_button(int btn, bool down, int x, int y);
void mouse_move_abs(int x, int y);
void mouse_move_rel(int dx, int dy);
void mouse_wheel(int notches, int x, int y);

}  // namespace eng
