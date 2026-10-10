#pragma once
#include <cstddef>
#include <cstdint>

namespace mcfm {

// The launcher's hook table in a converted game image (tools/launcher/mcfm_image.py dylib
// --hooks): the first 16-byte aligned address after the last section of __DATA, up to the end
// of the segment. Hook i jumps through table[i]. Both read only the header + load commands.
uintptr_t hook_table_address(const void *header);   // unslid; 0 if there is no __DATA
size_t hook_table_capacity(const void *header);     // entries; 0 if there is no __DATA

}  // namespace mcfm
