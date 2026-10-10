#include "macho_file.h"

#include <cstring>

namespace mcfm {
namespace loader {
namespace {

constexpr uint32_t kMagic64 = 0xFEEDFACF;
constexpr uint32_t kSegment64 = 0x19, kDyldInfo = 0x22, kDyldInfoOnly = 0x80000022, kCodeSignature = 0x1D;
constexpr uint32_t kUuid = 0x1B, kChainedFixups = 0x80000034;
constexpr uint32_t kLoadDylib = 0xC, kLoadWeakDylib = 0x80000018, kReexportDylib = 0x8000001F;
constexpr uint32_t kLazyLoadDylib = 0x20, kLoadUpwardDylib = 0x80000023;

template <class T> T rd(const uint8_t *p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

std::string fixed_name(const uint8_t *p) {  // 16-byte, maybe unterminated
  size_t n = 0;
  while (n < 16 && p[n]) n++;
  return std::string(reinterpret_cast<const char *>(p), n);
}

bool fail(std::string *error, const std::string &message) {
  if (error) *error = message;
  return false;
}

bool within(uint64_t off, uint64_t len, size_t size) { return off <= size && len <= size - off; }

}  // namespace

const Section *MachOFile::section(const char *seg, const char *name) const {
  for (const Segment &s : segments)
    if (s.name == seg)
      for (const Section &x : s.sections)
        if (x.name == name) return &x;
  return nullptr;
}

const Segment *MachOFile::segment(const char *name) const {
  for (const Segment &s : segments)
    if (s.name == name) return &s;
  return nullptr;
}

bool parse_macho(const uint8_t *data, size_t size, MachOFile *out, std::string *error) {
  *out = MachOFile();
  if (!data || size < 32) return fail(error, "shorter than a Mach-O header");
  if (rd<uint32_t>(data) != kMagic64) return fail(error, "not a 64-bit Mach-O");
  out->filetype = rd<uint32_t>(data + 12);
  out->flags = rd<uint32_t>(data + 24);
  uint32_t ncmds = rd<uint32_t>(data + 16), sizeofcmds = rd<uint32_t>(data + 20);
  if (!within(32, sizeofcmds, size)) return fail(error, "load commands past the end of the file");
  uint64_t off = 32, end = 32 + uint64_t(sizeofcmds);
  for (uint32_t i = 0; i < ncmds; i++) {
    if (off + 8 > end) return fail(error, "more load commands than sizeofcmds holds");
    const uint8_t *c = data + off;
    uint32_t cmd = rd<uint32_t>(c), cmdsize = rd<uint32_t>(c + 4);
    if (cmdsize < 8 || off + cmdsize > end) return fail(error, "bad cmdsize " + std::to_string(cmdsize) + " at offset " + std::to_string(off));
    if (cmd == kSegment64) {
      if (cmdsize < 72) return fail(error, "segment command too short");
      Segment s;
      s.name = fixed_name(c + 8);
      s.vmaddr = rd<uint64_t>(c + 24);
      s.vmsize = rd<uint64_t>(c + 32);
      s.fileoff = rd<uint64_t>(c + 40);
      s.filesize = rd<uint64_t>(c + 48);
      s.maxprot = rd<uint32_t>(c + 56);
      s.initprot = rd<uint32_t>(c + 60);
      uint32_t nsects = rd<uint32_t>(c + 64);
      if (72 + uint64_t(nsects) * 80 > cmdsize) return fail(error, "segment " + s.name + ": sections past cmdsize");
      if (!within(s.fileoff, s.filesize, size)) return fail(error, "segment " + s.name + " past the end of the file");
      for (uint32_t k = 0; k < nsects; k++) {
        const uint8_t *x = c + 72 + 80 * k;
        Section sec;
        sec.name = fixed_name(x);
        sec.segment = fixed_name(x + 16);
        sec.addr = rd<uint64_t>(x + 32);
        sec.size = rd<uint64_t>(x + 40);
        sec.offset = rd<uint32_t>(x + 48);
        sec.flags = rd<uint32_t>(x + 64);
        s.sections.push_back(sec);
      }
      out->segments.push_back(s);
    } else if (cmd == kDyldInfo || cmd == kDyldInfoOnly) {
      if (cmdsize < 48) return fail(error, "dyld info command too short");
      DyldInfo &d = out->dyld_info;
      uint32_t *f[] = {&d.rebase_off, &d.rebase_size, &d.bind_off, &d.bind_size, &d.weak_bind_off,
                       &d.weak_bind_size, &d.lazy_bind_off, &d.lazy_bind_size, &d.export_off, &d.export_size};
      for (int k = 0; k < 10; k++) *f[k] = rd<uint32_t>(c + 8 + 4 * k);
      for (int k = 0; k < 10; k += 2)
        if (!within(*f[k], *f[k + 1], size)) return fail(error, "dyld info past the end of the file");
      out->has_dyld_info = true;
    } else if (cmd == kLoadDylib || cmd == kLoadWeakDylib || cmd == kReexportDylib || cmd == kLazyLoadDylib ||
               cmd == kLoadUpwardDylib) {
      uint32_t name_off = cmdsize >= 12 ? rd<uint32_t>(c + 8) : 0;
      if (name_off < 24 || name_off >= cmdsize) return fail(error, "dylib command name past cmdsize");
      const char *name = reinterpret_cast<const char *>(c + name_off);
      out->dylibs.push_back(std::string(name, strnlen(name, cmdsize - name_off)));
      out->weak_dylibs.push_back(cmd == kLoadWeakDylib);
    } else if (cmd == kCodeSignature) {
      if (cmdsize < 16) return fail(error, "code signature command too short");
      out->code_signature_off = rd<uint32_t>(c + 8);
      out->code_signature_size = rd<uint32_t>(c + 12);
      if (!within(out->code_signature_off, out->code_signature_size, size)) return fail(error, "code signature past the end of the file");
    } else if (cmd == kUuid) {
      if (cmdsize < 24) return fail(error, "uuid command too short");
      std::memcpy(out->uuid, c + 8, 16);
    } else if (cmd == kChainedFixups) {
      out->has_chained_fixups = true;
    }
    off += cmdsize;
  }
  return true;
}

}  // namespace loader
}  // namespace mcfm
