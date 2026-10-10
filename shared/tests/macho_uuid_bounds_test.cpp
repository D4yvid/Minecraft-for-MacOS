// is_expected_game_image(buffer, size): never reads past `size`, whatever the header claims.
#include "macho_uuid.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const uint8_t kGameUuid[16] = {0x01, 0xDF, 0xB4, 0x89, 0xA8, 0x81, 0x3B, 0xDD,
                                      0x8F, 0x98, 0x6F, 0x01, 0x6E, 0x40, 0x96, 0x25};

template <class T> static void put(std::vector<uint8_t> *b, size_t off, T v) { std::memcpy(b->data() + off, &v, sizeof v); }

// mach_header_64 + one LC_UUID with the game's UUID.
static std::vector<uint8_t> game_header() {
  std::vector<uint8_t> b(32 + 24, 0);
  put<uint32_t>(&b, 0, 0xFEEDFACF);
  put<uint32_t>(&b, 16, 1);
  put<uint32_t>(&b, 20, 24);
  put<uint32_t>(&b, 32, 0x1B);
  put<uint32_t>(&b, 36, 24);
  std::memcpy(b.data() + 40, kGameUuid, 16);
  return b;
}

int main() {
  std::vector<uint8_t> h = game_header();
  EXPECT(mcfm::is_expected_game_image(h.data(), h.size()));
  EXPECT(!mcfm::is_expected_game_image(h.data(), h.size() - 1));  // UUID cut off
  // Claims far more load commands than the buffer holds: refused, no out-of-bounds read
  // (run under AddressSanitizer by the Makefile rule).
  std::vector<uint8_t> big = h;
  put<uint32_t>(&big, 16, 1000);
  put<uint32_t>(&big, 20, 0x10000000);
  put<uint32_t>(&big, 32, 0x19);
  put<uint32_t>(&big, 36, 0x01000000);
  EXPECT(!mcfm::is_expected_game_image(big.data(), big.size()));
  EXPECT(!mcfm::is_expected_game_image(h.data(), 16));  // shorter than a header
  EXPECT(!mcfm::is_expected_game_image(nullptr, 0));
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("macho_uuid_bounds_test: all passed\n");
  return 0;
}
