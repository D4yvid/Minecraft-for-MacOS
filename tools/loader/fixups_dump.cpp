// fixups_dump <image>: our decoder's fixups, one per line: "<kind> 0x<unslid address> [<lib>/<symbol>[ + 0x<addend>]]"
// (lib as dyld_info names it: the install name's last component up to the first '.').
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "fixups.h"

using namespace mcfm::loader;

int main(int argc, char **argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: fixups_dump <image>\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  MachOFile m;
  std::string err;
  std::vector<Fixup> fx;
  if (!parse_macho(file.data(), file.size(), &m, &err) || !decode_fixups(file.data(), file.size(), m, &fx, &err)) {
    std::fprintf(stderr, "fixups_dump: %s\n", err.c_str());
    return 1;
  }
  const char *kinds[] = {"rebase", "bind", "lazy-bind", "weak-bind"};
  for (const Fixup &x : fx) {
    uint64_t addr = m.segments[x.segment].vmaddr + x.offset;
    if (x.kind == FixupKind::Rebase) { std::printf("rebase 0x%llX\n", (unsigned long long)addr); continue; }
    std::string lib = x.ordinal > 0 && x.ordinal <= (int)m.dylibs.size() ? m.dylibs[x.ordinal - 1] : x.ordinal == 0 ? "self" : "flat";
    lib = lib.substr(lib.rfind('/') + 1);
    lib = lib.substr(0, lib.find('.'));
    std::printf("%s 0x%llX %s/%s", kinds[(int)x.kind], (unsigned long long)addr, lib.c_str(), x.symbol.c_str());
    if (x.addend) std::printf(" + 0x%llX", (unsigned long long)x.addend);
    std::printf("\n");
  }
  return 0;
}
