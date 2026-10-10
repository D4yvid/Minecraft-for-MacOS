#include "fixups.h"

#include <cstring>

namespace mcfm {
namespace loader {
namespace {

// Opcodes and immediates (mach-o/loader.h).
enum : uint8_t {
  kOpcodeMask = 0xF0, kImmMask = 0x0F,
  kRebaseDone = 0x00, kRebaseSetType = 0x10, kRebaseSetSegOff = 0x20, kRebaseAddAddrUleb = 0x30,
  kRebaseAddAddrImmScaled = 0x40, kRebaseImmTimes = 0x50, kRebaseUlebTimes = 0x60,
  kRebaseAddAddrUlebAfter = 0x70, kRebaseUlebTimesSkipping = 0x80,
  kBindDone = 0x00, kBindOrdinalImm = 0x10, kBindOrdinalUleb = 0x20, kBindOrdinalSpecial = 0x30,
  kBindSymbol = 0x40, kBindSetType = 0x50, kBindAddendSleb = 0x60, kBindSetSegOff = 0x70,
  kBindAddAddrUleb = 0x80, kBindDo = 0x90, kBindDoAddAddrUleb = 0xA0, kBindDoAddAddrImmScaled = 0xB0,
  kBindDoUlebTimesSkipping = 0xC0, kBindThreaded = 0xD0,
};
constexpr uint32_t kFlagWeakImport = 0x1, kFlagNonWeakDefinition = 0x8;
constexpr uint8_t kTypePointer = 1;  // REBASE_TYPE_POINTER / BIND_TYPE_POINTER
constexpr uint32_t kProtWrite = 0x2;

struct Reader {
  const uint8_t *p, *end;
  std::string *error;
  bool ok;
  Reader(const uint8_t *begin, const uint8_t *stop, std::string *err) : p(begin), end(stop), error(err), ok(true) {}
  bool done() const { return p >= end; }
  uint8_t byte() {
    if (p >= end) return bad("stream ends early"), 0;
    return *p++;
  }
  uint64_t uleb() {
    uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
      if (p >= end) return bad("truncated uleb128"), 0;
      uint8_t b = *p++;
      if (shift == 63 && (b & 0x7E)) return bad("uleb128 too big"), 0;  // only bit 63 fits
      v |= uint64_t(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
    }
    return bad("uleb128 too long"), 0;
  }
  int64_t sleb() {  // unsigned arithmetic: no signed-shift overflow at the sign bit
    uint64_t v = 0;
    int shift = 0;
    uint8_t b;
    do {
      if (p >= end || shift >= 64) return bad("truncated sleb128"), 0;
      b = *p++;
      v |= uint64_t(b & 0x7F) << shift;
      shift += 7;
    } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) v |= ~uint64_t(0) << shift;
    int64_t r;
    std::memcpy(&r, &v, sizeof r);
    return r;
  }
  std::string cstring() {
    const uint8_t *s = p;
    while (p < end && *p) p++;
    if (p >= end) return bad("unterminated symbol name"), std::string();
    std::string r(reinterpret_cast<const char *>(s), p - s);
    p++;
    return r;
  }
  bool bad(const char *message) {
    if (ok && error) *error = message;
    ok = false;
    p = end;
    return false;
  }
};

struct Decoder {
  const uint8_t *file;
  size_t size;
  const MachOFile &m;
  std::vector<Fixup> *out;
  std::string *error;

  bool emit(Reader &r, FixupKind kind, int segment, uint64_t offset, int ordinal, const std::string &symbol,
            int64_t addend, bool weak_import) {
    if (segment < 0 || segment >= static_cast<int>(m.segments.size())) return r.bad("fixup segment index out of range");
    const Segment &s = m.segments[segment];
    if (offset > s.vmsize || s.vmsize - offset < 8) return r.bad("fixup outside its segment");
    if (!(s.initprot & kProtWrite)) return r.bad("fixup in a segment that is not writable");
    Fixup f = {kind, segment, offset, ordinal, symbol, addend, weak_import};
    out->push_back(f);
    return true;
  }

  bool rebases(uint32_t off, uint32_t len) {
    Reader r(file + off, file + off + len, error);
    int seg = -1;
    uint64_t o = 0;
    while (r.ok && !r.done()) {
      uint8_t b = r.byte(), op = b & kOpcodeMask, imm = b & kImmMask;
      switch (op) {
        case kRebaseDone: return r.ok;
        case kRebaseSetType:
          if (imm != kTypePointer) return r.bad("rebase type other than pointer");
          break;
        case kRebaseSetSegOff: seg = imm; o = r.uleb(); break;
        case kRebaseAddAddrUleb: o += r.uleb(); break;
        case kRebaseAddAddrImmScaled: o += uint64_t(imm) * 8; break;
        case kRebaseImmTimes:
          for (int i = 0; i < imm && r.ok; i++, o += 8) emit(r, FixupKind::Rebase, seg, o, 0, std::string(), 0, false);
          break;
        case kRebaseUlebTimes: {
          uint64_t n = r.uleb();
          for (uint64_t i = 0; i < n && r.ok; i++, o += 8) emit(r, FixupKind::Rebase, seg, o, 0, std::string(), 0, false);
          break;
        }
        case kRebaseAddAddrUlebAfter:
          emit(r, FixupKind::Rebase, seg, o, 0, std::string(), 0, false);
          o += r.uleb() + 8;
          break;
        case kRebaseUlebTimesSkipping: {
          uint64_t n = r.uleb(), skip = r.uleb();
          for (uint64_t i = 0; i < n && r.ok; i++, o += skip + 8) emit(r, FixupKind::Rebase, seg, o, 0, std::string(), 0, false);
          break;
        }
        default: return r.bad("unknown rebase opcode");
      }
    }
    return r.ok;
  }

  // lazy: DONE ends one entry and decoding continues; regular/weak: DONE ends the stream.
  bool binds(uint32_t off, uint32_t len, FixupKind kind) {
    Reader r(file + off, file + off + len, error);
    int seg = -1, ordinal = 0;
    uint64_t o = 0;
    int64_t addend = 0;
    uint32_t flags = 0;
    std::string symbol;
    while (r.ok && !r.done()) {
      uint8_t b = r.byte(), op = b & kOpcodeMask, imm = b & kImmMask;
      switch (op) {
        case kBindDone:
          if (kind != FixupKind::LazyBind) return r.ok;
          break;
        case kBindOrdinalImm: ordinal = imm; break;
        case kBindOrdinalUleb: {
          uint64_t o2 = r.uleb();
          if (o2 > m.dylibs.size()) return r.bad("bind dylib ordinal beyond the dylib list");
          ordinal = static_cast<int>(o2);
          break;
        }
        case kBindOrdinalSpecial: ordinal = imm ? static_cast<int>(int8_t(kOpcodeMask | imm)) : 0; break;
        case kBindSymbol:
          symbol = r.cstring();
          flags = imm;
          break;
        case kBindSetType:
          if (imm != kTypePointer) return r.bad("bind type other than pointer");
          break;
        case kBindAddendSleb: addend = r.sleb(); break;
        case kBindSetSegOff: seg = imm; o = r.uleb(); break;
        case kBindAddAddrUleb: o += r.uleb(); break;
        case kBindDo:
          bind(r, kind, seg, o, ordinal, symbol, addend, flags);
          o += 8;
          break;
        case kBindDoAddAddrUleb:
          bind(r, kind, seg, o, ordinal, symbol, addend, flags);
          o += r.uleb() + 8;
          break;
        case kBindDoAddAddrImmScaled:
          bind(r, kind, seg, o, ordinal, symbol, addend, flags);
          o += uint64_t(imm) * 8 + 8;
          break;
        case kBindDoUlebTimesSkipping: {
          uint64_t n = r.uleb(), skip = r.uleb();
          for (uint64_t i = 0; i < n && r.ok; i++, o += skip + 8) bind(r, kind, seg, o, ordinal, symbol, addend, flags);
          break;
        }
        case kBindThreaded: return r.bad("threaded binds (chained fixups) are not supported");
        default: return r.bad("unknown bind opcode");
      }
    }
    return r.ok;
  }

  void bind(Reader &r, FixupKind kind, int seg, uint64_t o, int ordinal, const std::string &symbol, int64_t addend,
            uint32_t flags) {
    if (symbol.empty()) { r.bad("bind without a symbol"); return; }
    if (ordinal > static_cast<int>(m.dylibs.size())) { r.bad("bind dylib ordinal beyond the dylib list"); return; }
    if (kind == FixupKind::WeakBind && (flags & kFlagNonWeakDefinition)) return;  // a strong definition, no slot
    emit(r, kind, seg, o, ordinal, symbol, addend, (flags & kFlagWeakImport) != 0);
  }
};

}  // namespace

bool decode_fixups(const uint8_t *file, size_t size, const MachOFile &m, std::vector<Fixup> *out, std::string *error) {
  out->clear();
  if (m.has_chained_fixups) {
    if (error) *error = "chained fixups are not supported";
    return false;
  }
  if (!m.has_dyld_info) {
    if (error) *error = "no LC_DYLD_INFO";
    return false;
  }
  const DyldInfo &d = m.dyld_info;
  uint64_t streams[][2] = {{d.rebase_off, d.rebase_size}, {d.bind_off, d.bind_size}, {d.lazy_bind_off, d.lazy_bind_size},
                           {d.weak_bind_off, d.weak_bind_size}};
  for (auto &s : streams)
    if (s[0] > size || s[1] > size - s[0]) {
      if (error) *error = "fixup stream past the end of the file";
      return false;
    }
  Decoder dec = {file, size, m, out, error};
  return dec.rebases(d.rebase_off, d.rebase_size) && dec.binds(d.bind_off, d.bind_size, FixupKind::Bind) &&
         dec.binds(d.lazy_bind_off, d.lazy_bind_size, FixupKind::LazyBind) &&
         dec.binds(d.weak_bind_off, d.weak_bind_size, FixupKind::WeakBind);
}

bool find_export(const uint8_t *file, size_t size, const MachOFile &m, const std::string &symbol, uint64_t *offset,
                 uint32_t *flags) {
  const DyldInfo &d = m.dyld_info;
  if (!m.has_dyld_info || d.export_size == 0 || d.export_off > size || d.export_size > size - d.export_off) return false;
  const uint8_t *trie = file + d.export_off, *trie_end = trie + d.export_size;
  uint64_t node = 0;
  size_t matched = 0;
  for (int depth = 0; depth < 128; depth++) {
    Reader r(trie + node, trie_end, nullptr);
    if (node >= d.export_size) return false;
    uint64_t terminal = r.uleb();
    if (!r.ok || terminal > static_cast<uint64_t>(trie_end - r.p)) return false;
    const uint8_t *children = r.p + terminal;
    if (matched == symbol.size()) {
      if (terminal == 0) return false;
      uint64_t f = r.uleb(), addr = r.uleb();
      if (!r.ok) return false;
      if (flags) *flags = static_cast<uint32_t>(f);
      if (offset) *offset = addr;
      return true;
    }
    if (children >= trie_end) return false;
    Reader c(children, trie_end, nullptr);
    uint8_t count = c.byte();
    bool found = false;
    for (int i = 0; i < count && c.ok && !found; i++) {
      const uint8_t *edge = c.p;
      while (c.p < trie_end && *c.p) c.p++;
      if (c.p >= trie_end) return false;
      size_t len = c.p - edge;
      c.p++;
      uint64_t child = c.uleb();
      if (len && symbol.compare(matched, len, reinterpret_cast<const char *>(edge), len) == 0) {
        matched += len;
        node = child;
        found = true;
      }
    }
    if (!found) return false;
  }
  return false;
}

}  // namespace loader
}  // namespace mcfm
