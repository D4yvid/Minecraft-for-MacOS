#include "macho_uuid.h"

#include <cstdint>
#include <cstring>

namespace mcpekbm {
namespace {

constexpr uint32_t kMagic64 = 0xFEEDFACF;
constexpr uint32_t kLcUuid = 0x1B;
constexpr uint32_t kHeaderSize = 32;
// minecraftpe2 0.15.10 arm64 slice: 01DFB489-A881-3BDD-8F98-6F016E409625
constexpr uint8_t kGameUuid[16] = {0x01, 0xDF, 0xB4, 0x89, 0xA8, 0x81, 0x3B, 0xDD,
                                   0x8F, 0x98, 0x6F, 0x01, 0x6E, 0x40, 0x96, 0x25};

uint32_t u32(const uint8_t *p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

}  // namespace

bool is_expected_game_image(const void *header) {
  const uint8_t *h = static_cast<const uint8_t *>(header);
  if (!h || u32(h) != kMagic64) return false;
  uint32_t ncmds = u32(h + 16), sizeofcmds = u32(h + 20);
  uint32_t off = kHeaderSize, end = kHeaderSize + sizeofcmds;
  for (uint32_t i = 0; i < ncmds && off + 8 <= end; i++) {
    uint32_t cmd = u32(h + off), cmdsize = u32(h + off + 4);
    if (cmdsize < 8) return false;
    if (cmd == kLcUuid && cmdsize >= 24) return std::memcmp(h + off + 8, kGameUuid, 16) == 0;
    off += cmdsize;
  }
  return false;
}

}  // namespace mcpekbm
