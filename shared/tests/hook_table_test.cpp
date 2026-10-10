// Hook table: locating it in a header (synthetic Mach-O headers) and decoding a patched entry.
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

template <class T> void put(std::vector<uint8_t> *b, size_t off, T v) { std::memcpy(b->data() + off, &v, sizeof v); }

// mach_header_64 + one LC_SEGMENT_64 `seg` (vmaddr, vmsize) with one section [addr, addr+size).
std::vector<uint8_t> header(const char *seg, uint64_t vmaddr, uint64_t vmsize, uint64_t addr, uint64_t size,
                            uint32_t cmdsize = 72 + 80) {
  std::vector<uint8_t> b(32 + 72 + 80, 0);
  put<uint32_t>(&b, 0, 0xFEEDFACF);
  put<uint32_t>(&b, 16, 1);           // ncmds
  put<uint32_t>(&b, 20, 72 + 80);     // sizeofcmds
  put<uint32_t>(&b, 32, 0x19);        // LC_SEGMENT_64
  put<uint32_t>(&b, 36, cmdsize);
  std::strncpy(reinterpret_cast<char *>(b.data() + 40), seg, 16);
  put<uint64_t>(&b, 56, vmaddr);
  put<uint64_t>(&b, 64, vmsize);
  put<uint32_t>(&b, 96, 1);           // nsects
  put<uint64_t>(&b, 104 + 32, addr);  // section addr
  put<uint64_t>(&b, 104 + 40, size);  // section size
  return b;
}

}  // namespace

int main() {
  std::vector<uint8_t> h = header("__DATA", 0x2000, 0x1000, 0x2000, 0x123);
  EXPECT(mcfm::hook_table_address(h.data()) == 0x2130);
  EXPECT(mcfm::hook_table_capacity(h.data()) == (0x3000 - 0x2130) / 8);
  std::vector<uint8_t> text = header("__TEXT", 0x2000, 0x1000, 0x2000, 0x123);
  EXPECT(mcfm::hook_table_address(text.data()) == 0 && mcfm::hook_table_capacity(text.data()) == 0);
  std::vector<uint8_t> past = header("__DATA", 0x2000, 0x1000, 0x2000, 0x1000 + 0x10);
  EXPECT(mcfm::hook_table_address(past.data()) == 0 && mcfm::hook_table_capacity(past.data()) == 0);
  std::vector<uint8_t> bad = header("__DATA", 0x2000, 0x1000, 0x2000, 0x123, 0);
  EXPECT(mcfm::hook_table_address(bad.data()) == 0);
  EXPECT(mcfm::hook_table_address(nullptr) == 0);

  // adrp x16, page(0x5008) / ldr x16, [x16, #8] / br x16 at pc 0x1000 -> slot 0x5008.
  const uint32_t forward[3] = {0x90000030, 0xF9400610, 0xD61F0200};
  EXPECT(mcfm::hook_slot(forward, 0x1000) == 0x5008);
  // Negative page delta: at pc 0x5000 -> slot 0x1010.
  const uint32_t backward[3] = {0x90FFFFF0, 0xF9400A10, 0xD61F0200};
  EXPECT(mcfm::hook_slot(backward, 0x5000) == 0x1010);
  // Not a hook (an ordinary prologue) -> 0.
  const uint32_t prologue[3] = {0xA9BF7BFD, 0x910003FD, 0xD65F03C0};
  EXPECT(mcfm::hook_slot(prologue, 0x1000) == 0);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("hook_table_test: all passed\n");
  return 0;
}
