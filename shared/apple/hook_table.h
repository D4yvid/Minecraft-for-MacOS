#pragma once
#include <cstddef>
#include <cstdint>

namespace mcfm {

// The launcher's hook table in a converted game image (tools/launcher/mcfm_image.py dylib
// --hooks): the first 16-byte aligned address after the last section of __DATA, up to the end
// of the segment. Hook i jumps through table[i]. Both read only the header + load commands.
uintptr_t hook_table_address(const void *header);   // unslid; 0 if there is no __DATA
size_t hook_table_capacity(const void *header);     // entries; 0 if there is no __DATA

// The table slot a patched entry jumps through: `code` holds the 12 bytes at address `pc`
// (adrp x16 / ldr x16, [x16, #off] / br x16, as written by mcfm_image.py dylib --hooks).
// 0 if the bytes are not that sequence (the image was converted without this hook).
uintptr_t hook_slot(const void *code, uintptr_t pc);

}  // namespace mcfm
