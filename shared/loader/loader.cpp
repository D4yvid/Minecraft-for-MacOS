#include "loader.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "hook_table.h"

namespace mcfm {
namespace loader {
namespace {

constexpr uint32_t kCpuArm64 = 0x0100000C;
constexpr uint32_t kSectionTypeMask = 0xFF, kModInitFuncPointers = 0x9, kInitFuncOffsets = 0x16;
constexpr uint32_t kFirstThreadLocalType = 0x11, kLastThreadLocalType = 0x15;
constexpr uint32_t kExportKindMask = 0x3, kExportRegular = 0x0, kExportReexport = 0x8, kExportStubAndResolver = 0x10;

bool fail(std::string *error, const std::string &message) {
  if (error) *error = message;
  return false;
}

uint64_t read64(const uint8_t *p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

void write64(uint8_t *p, uint64_t v) { std::memcpy(p, &v, 8); }

uint64_t round_up(uint64_t v, uint64_t page) { return (v + page - 1) & ~(page - 1); }

}  // namespace

std::string library_short_name(const std::string &install_name) {
  std::string base = install_name.substr(install_name.rfind('/') + 1);
  return base.substr(0, base.find('.'));
}

bool load_image(LoaderOS &os, int fd, const uint8_t *file, size_t size, const LoadOptions &opt, Image *out,
                std::string *error) {
  Image img;
  if (!parse_macho(file, size, &img.macho, error)) return false;
  const MachOFile &m = img.macho;
  uint32_t cpu = 0;
  std::memcpy(&cpu, file + 4, 4);
  if (cpu != kCpuArm64) return fail(error, "not an arm64 image");
  std::vector<Fixup> fixups;
  if (!decode_fixups(file, size, m, &fixups, error)) return false;
  const Segment *text = m.segment("__TEXT");
  if (!text || text->fileoff != 0) return fail(error, "no __TEXT segment at file offset 0");
  for (const Segment &s : m.segments)
    for (const Section &sec : s.sections) {
      uint32_t type = sec.flags & kSectionTypeMask;
      if (type >= kFirstThreadLocalType && type <= kLastThreadLocalType)
        return fail(error, "thread-local variables are not supported (" + sec.segment + "," + sec.name + ")");
    }
  if (!m.code_signature_size && os.requires_code_signature())
    return fail(error, "the image has no code signature and this system requires one");

  // Reserve the whole image, then map each segment over it.
  uint64_t lo = UINT64_MAX, hi = 0;
  for (const Segment &s : m.segments)
    if (s.vmsize) {
      if (s.vmaddr < lo) lo = s.vmaddr;
      if (s.vmaddr + s.vmsize > hi) hi = s.vmaddr + s.vmsize;
    }
  if (lo >= hi) return fail(error, "no segments");
  uint8_t *base = os.reserve(hi - lo);
  if (!base) return fail(error, "cannot reserve address space");
  img.slide = reinterpret_cast<intptr_t>(base) - static_cast<intptr_t>(lo);
  uint64_t mapped_end = 0;
  for (const Segment &s : m.segments)
    // __LINKEDIT holds the signature itself, so the signature cannot cover it.
    if (s.initprot && s.name != "__LINKEDIT" && s.fileoff + s.filesize > mapped_end) mapped_end = s.fileoff + s.filesize;
  if (m.code_signature_size && !os.register_code_signature(fd, m.code_signature_off, m.code_signature_size, mapped_end))
    return fail(error, "cannot register the code signature");
  uint64_t page = os.page_size();
  for (const Segment &s : m.segments) {
    if (!s.vmsize || !s.initprot) continue;  // e.g. __MCFM_PAD stays reserved and inaccessible
    uint8_t *at = reinterpret_cast<uint8_t *>(s.vmaddr + img.slide);
    uint64_t file_end = round_up(s.filesize, page);
    bool writable = (s.initprot & 2) != 0;
    // The rest of the last file page belongs to the zero-fill part: dyld zeroes it. A read-only
    // segment cannot be zeroed after mapping; it is fine only at the end of the file (mmap
    // zero-fills past it, e.g. __LINKEDIT).
    if (file_end != s.filesize && s.vmsize > s.filesize && !writable && s.fileoff + s.filesize < size)
      return fail(error, "read-only segment " + s.name + " has a partial last page");
    if (s.filesize && !os.map_file(at, s.filesize, static_cast<int>(s.initprot), fd, s.fileoff))
      return fail(error, "cannot map " + s.name);
    if (writable && file_end != s.filesize) std::memset(at + s.filesize, 0, std::min(file_end, s.vmsize) - s.filesize);
    if (s.vmsize > file_end && !os.map_zero(at + file_end, s.vmsize - file_end, static_cast<int>(s.initprot)))
      return fail(error, "cannot map zero-fill of " + s.name);
  }
  img.header = reinterpret_cast<uint8_t *>(text->vmaddr + img.slide);
  img.text_lo = reinterpret_cast<uintptr_t>(img.header);
  img.text_hi = img.text_lo + text->vmsize;
  if (opt.accept_header && !opt.accept_header(img.header)) return fail(error, "the mapped image was rejected");

  // Fixups, in dyld's order (decode_fixups returns them so).
  std::map<int, void *> libraries;
  for (const Fixup &f : fixups) {
    const Segment &s = m.segments[f.segment];
    uint8_t *slot = reinterpret_cast<uint8_t *>(s.vmaddr + img.slide + f.offset);
    if (f.kind == FixupKind::Rebase) {
      write64(slot, read64(slot) + static_cast<uint64_t>(img.slide));
      continue;
    }
    std::string name = f.symbol.size() > 1 && f.symbol[0] == '_' ? f.symbol.substr(1) : f.symbol;
    void *value = nullptr;
    if (f.kind == FixupKind::WeakBind) {
      // Coalescing as dyld: the first definition in load order, i.e. a host library's wins;
      // without one the slot keeps the image's own definition (set by rebase or bind).
      value = os.flat_symbol(name);
      uint64_t off = 0;
      uint32_t flags = 0;
      if (!value && find_export(file, size, m, f.symbol, &off, &flags) && (flags & ~kExportStubAndResolver) == kExportRegular)
        value = img.header + off;
      if (value) write64(slot, reinterpret_cast<uint64_t>(value) + static_cast<uint64_t>(f.addend));
      continue;
    }
    std::string where;
    bool weak_library_missing = false;
    if (f.ordinal > 0) {
      if (f.ordinal > static_cast<int>(m.dylibs.size())) return fail(error, "bind ordinal out of range for " + f.symbol);
      std::string lib = library_short_name(m.dylibs[f.ordinal - 1]);
      where = lib;
      auto it = libraries.find(f.ordinal);
      if (it == libraries.end()) it = libraries.insert(std::make_pair(f.ordinal, os.open_library(lib))).first;
      if (!it->second && !m.weak_dylibs[f.ordinal - 1]) return fail(error, "cannot load " + lib);
      weak_library_missing = !it->second;  // dyld binds every symbol of a missing weak library to 0
      value = it->second ? os.symbol(it->second, name) : nullptr;
    } else if (f.ordinal == 0) {
      where = "self";
      uint64_t off = 0;
      uint32_t flags = 0;
      if (find_export(file, size, m, f.symbol, &off, &flags)) {
        // Only a plain definition in this image: not a re-export, absolute or thread-local symbol.
        if ((flags & kExportReexport) || (flags & kExportKindMask) != kExportRegular || (flags & kExportStubAndResolver))
          return fail(error, "unsupported self-bind to " + f.symbol + " (re-export, absolute, resolver or thread-local)");
        value = img.header + off;
      }
    } else {
      where = "flat";
      value = os.flat_symbol(name);
    }
    // dyld's lazy-binding helper: every lazy pointer is bound here up front, so it is never
    // called (and modern libSystem no longer exports it).
    bool lazy_helper = f.symbol == "dyld_stub_binder";
    if (!value && !f.weak_import && !lazy_helper && !weak_library_missing) return fail(error, "missing symbol: " + where + ": " + f.symbol);
    write64(slot, value ? reinterpret_cast<uint64_t>(value) + static_cast<uint64_t>(f.addend) : 0);
  }

  // Hooks: the image must jump through the table for exactly these addresses.
  if (opt.hook_count) {
    uintptr_t table = mcfm::hook_table_address(img.header);
    if (!table || mcfm::hook_table_capacity(img.header) < opt.hook_count) return fail(error, "no hook table in the image");
    void **slots = reinterpret_cast<void **>(table + img.slide);
    for (size_t i = 0; i < opt.hook_count; i++) {
      uintptr_t entry = opt.hook_addresses[i] + img.slide;
      if (opt.hook_addresses[i] < text->vmaddr || opt.hook_addresses[i] + 12 > text->vmaddr + text->vmsize ||
          mcfm::hook_slot(reinterpret_cast<const void *>(entry), entry) != reinterpret_cast<uintptr_t>(&slots[i]))
        return fail(error, "hook " + (opt.hook_names ? std::string(opt.hook_names[i]) : std::to_string(i)) +
                               " is not installed in the image (rebuild with make app)");
      slots[i] = const_cast<void *>(opt.hook_replacements[i]);
    }
  }

  const Section *unwind = m.section("__TEXT", "__unwind_info");
  const Section *eh = m.section("__TEXT", "__eh_frame");
  if (!os.register_unwind(img.text_lo, img.text_lo, img.text_hi, unwind ? unwind->addr + img.slide : 0,
                          unwind ? unwind->size : 0, eh ? eh->addr + img.slide : 0, eh ? eh->size : 0))
    return fail(error, "cannot register unwind info (needs macOS 14+)");

  if (opt.run_initializers) {
    // Every initializer must point into __TEXT; check them all before calling any.
    std::vector<uintptr_t> initializers;
    for (const Segment &s : m.segments)
      for (const Section &sec : s.sections) {
        uint32_t type = sec.flags & kSectionTypeMask;
        if (type == kModInitFuncPointers)
          for (uint64_t i = 0; i < sec.size / 8; i++)
            initializers.push_back(static_cast<uintptr_t>(read64(reinterpret_cast<uint8_t *>(sec.addr + img.slide + 8 * i))));
        else if (type == kInitFuncOffsets)
          for (uint64_t i = 0; i < sec.size / 4; i++) {
            uint32_t offset;
            std::memcpy(&offset, reinterpret_cast<uint8_t *>(sec.addr + img.slide + 4 * i), 4);
            initializers.push_back(img.text_lo + offset);
          }
      }
    for (uintptr_t f : initializers)
      if (f < img.text_lo || f >= img.text_hi) return fail(error, "initializer outside __TEXT");
    typedef void (*Initializer)(int, const char **, const char **, const char **, void *);
    for (uintptr_t f : initializers) {
      reinterpret_cast<Initializer>(f)(opt.argc, opt.argv, opt.envp, opt.apple, nullptr);
      if (opt.initializers_run) ++*opt.initializers_run;
    }
  }
  *out = img;
  return true;
}

}  // namespace loader
}  // namespace mcfm
