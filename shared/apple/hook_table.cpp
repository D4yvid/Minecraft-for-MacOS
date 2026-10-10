#include "hook_table.h"

#include <cstring>

namespace mcfm {
namespace {

constexpr uint32_t kMagic64 = 0xFEEDFACF, kLcSegment64 = 0x19, kHeaderSize = 32;

template <class T> T rd(const uint8_t *p) { T v; std::memcpy(&v, p, sizeof v); return v; }

// Unslid [table, segment end) of __DATA; false if absent or malformed.
bool data_tail(const void *header, uint64_t *table, uint64_t *end) {
  const uint8_t *h = static_cast<const uint8_t *>(header);
  if (!h || rd<uint32_t>(h) != kMagic64) return false;
  uint32_t ncmds = rd<uint32_t>(h + 16), sizeofcmds = rd<uint32_t>(h + 20);
  uint32_t off = kHeaderSize, limit = kHeaderSize + sizeofcmds;
  for (uint32_t i = 0; i < ncmds && off + 8 <= limit; i++) {
    uint32_t cmd = rd<uint32_t>(h + off), size = rd<uint32_t>(h + off + 4);
    if (size < 8 || off + size > limit) return false;
    if (cmd == kLcSegment64 && size >= 72 && std::strncmp(reinterpret_cast<const char *>(h + off + 8), "__DATA", 16) == 0) {
      uint64_t vmaddr = rd<uint64_t>(h + off + 24), vmsize = rd<uint64_t>(h + off + 32);
      uint32_t nsects = rd<uint32_t>(h + off + 64);
      if (72 + 80ull * nsects > size) return false;
      uint64_t last = vmaddr;
      for (uint32_t k = 0; k < nsects; k++) {
        const uint8_t *s = h + off + 72 + 80 * k;
        uint64_t sect_end = rd<uint64_t>(s + 32) + rd<uint64_t>(s + 40);
        if (sect_end > last) last = sect_end;
      }
      *table = (last + 15) & ~uint64_t(15);
      *end = vmaddr + vmsize;
      return *table <= *end;
    }
    off += size;
  }
  return false;
}

}  // namespace

uintptr_t hook_table_address(const void *header) {
  uint64_t table, end;
  return data_tail(header, &table, &end) ? static_cast<uintptr_t>(table) : 0;
}

uintptr_t hook_slot(const void *code, uintptr_t pc) {
  const uint8_t *c = static_cast<const uint8_t *>(code);
  uint32_t adrp = rd<uint32_t>(c), ldr = rd<uint32_t>(c + 4), br = rd<uint32_t>(c + 8);
  if ((adrp & 0x9F00001F) != 0x90000010 || (ldr & 0xFFC003FF) != 0xF9400210 || br != 0xD61F0200) return 0;
  int64_t pages = static_cast<int64_t>((((adrp >> 5) & 0x7FFFF) << 2) | ((adrp >> 29) & 3));
  if (pages & (int64_t(1) << 20)) pages -= int64_t(1) << 21;  // sign-extend 21 bits
  uintptr_t page = (pc & ~uintptr_t(0xFFF)) + static_cast<uintptr_t>(pages * 4096);
  return page + ((ldr >> 10) & 0xFFF) * 8;
}

size_t hook_table_capacity(const void *header) {
  uint64_t table, end;
  return data_tail(header, &table, &end) ? static_cast<size_t>((end - table) / 8) : 0;
}

}  // namespace mcfm
