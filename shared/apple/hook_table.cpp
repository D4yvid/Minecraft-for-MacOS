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

size_t hook_table_capacity(const void *header) {
  uint64_t table, end;
  return data_tail(header, &table, &end) ? static_cast<size_t>((end - table) / 8) : 0;
}

}  // namespace mcfm
