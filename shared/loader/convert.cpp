#include "convert.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mcfm {
namespace loader {
namespace {

constexpr uint32_t kMagic64 = 0xFEEDFACF, kCpuArm64 = 0x0100000C;
constexpr uint32_t kExecute = 2, kDylib = 6, kPie = 0x200000;
constexpr uint32_t kSegment64 = 0x19, kLoadDylib = 0xC, kIdDylib = 0xD, kLoadDylinker = 0xE;
constexpr uint32_t kLoadWeakDylib = 0x80000018, kMain = 0x80000028, kBuildVersion = 0x32;
constexpr uint32_t kEncryptionInfo = 0x21, kEncryptionInfo64 = 0x2C;
constexpr uint32_t kReexportDylib = 0x8000001F, kLazyLoadDylib = 0x20, kLoadUpwardDylib = 0x80000023;
constexpr uint32_t kPlatformMacOS = 1, kMacOS11 = 0x000B0000;
constexpr uint64_t kPadSize = 0x4000;
constexpr const char kImageId[] = "@rpath/libminecraftpe.dylib";

bool fail(std::string *error, const std::string &message) {
  if (error) *error = message;
  return false;
}

uint32_t u32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t u64(const uint8_t *p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
void put32(uint8_t *p, uint32_t v) { std::memcpy(p, &v, 4); }
void put64(uint8_t *p, uint64_t v) { std::memcpy(p, &v, 8); }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t *p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

bool is_dylib_load(uint32_t cmd) {
  return cmd == kLoadDylib || cmd == kLoadWeakDylib || cmd == kReexportDylib || cmd == kLazyLoadDylib ||
         cmd == kLoadUpwardDylib;
}
bool is_version_min(uint32_t cmd) { return cmd == 0x24 || cmd == 0x25 || cmd == 0x2F || cmd == 0x30; }

std::string segment_name(const std::vector<uint8_t> &c) {
  const char *p = reinterpret_cast<const char *>(c.data() + 8);
  return std::string(p, strnlen(p, 16));
}

// '/usr/lib/libobjc.A.dylib' -> 'libobjc', '.../UIKit.framework/UIKit' -> 'UIKit'.
std::string short_name(const std::string &path) {
  std::string base = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
  return base.substr(0, base.find('.'));
}

std::vector<uint8_t> dylib_command(uint32_t cmd, const std::string &name) {
  uint32_t size = static_cast<uint32_t>((24 + name.size() + 1 + 7) & ~size_t(7));
  std::vector<uint8_t> c(size, 0);
  put32(&c[0], cmd);
  put32(&c[4], size);
  put32(&c[8], 24);
  put32(&c[12], 2);
  put32(&c[16], 0x10000);
  put32(&c[20], 0x10000);
  std::memcpy(&c[24], name.data(), name.size());
  return c;
}

struct SegmentInfo {
  uint64_t vmaddr = 0, vmsize = 0, fileoff = 0;
  std::vector<std::pair<uint64_t, uint64_t>> sections;  // addr, size
};

bool segment_info(const std::vector<std::vector<uint8_t>> &cmds, const char *name, SegmentInfo *out) {
  for (const auto &c : cmds)
    if (u32(&c[0]) == kSegment64 && segment_name(c) == name) {
      out->vmaddr = u64(&c[24]);
      out->vmsize = u64(&c[32]);
      out->fileoff = u64(&c[40]);
      uint32_t n = u32(&c[64]);
      for (uint32_t k = 0; k < n; k++) out->sections.emplace_back(u64(&c[72 + 80 * k + 32]), u64(&c[72 + 80 * k + 40]));
      return true;
    }
  return false;
}

bool is_return_or_branch(uint32_t w) {
  return (w & 0xFFFFFC1F) == 0xD65F0000 || (w & 0xFFFFFC1F) == 0xD61F0000 || (w & 0xFC000000) == 0x14000000;
}

std::string hex(uint64_t v) {
  char b[32];
  std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(v));
  return b;
}

bool check_hooks(const std::vector<uint8_t> &data, const std::vector<ConvertHook> &hooks, const SegmentInfo &t,
                 std::string *error) {
  std::vector<ConvertHook> by_addr = hooks;
  std::stable_sort(by_addr.begin(), by_addr.end(),
                   [](const ConvertHook &a, const ConvertHook &b) { return a.address < b.address; });
  for (size_t i = 0; i < by_addr.size(); i++) {
    const ConvertHook &h = by_addr[i];
    if (h.address % 4) return fail(error, "hook " + h.name + " at " + hex(h.address) + " is not 4-byte aligned");
    if (!(t.vmaddr <= h.address && h.address + 12 <= t.vmaddr + t.vmsize))
      return fail(error, "hook " + h.name + " at " + hex(h.address) + " is not in __TEXT");
    if (i + 1 < by_addr.size() && by_addr[i + 1].address < h.address + 12)
      return fail(error, "hooks " + h.name + " and " + by_addr[i + 1].name + " overlap (" + hex(h.address) + ", " +
                             hex(by_addr[i + 1].address) + ")");
    uint64_t off = h.address - t.vmaddr + t.fileoff;
    if (off + 12 > data.size()) return fail(error, "hook " + h.name + " is past the end of the file");
    if (is_return_or_branch(u32(&data[off])) || is_return_or_branch(u32(&data[off + 4])))
      return fail(error, "hook " + h.name + " at " + hex(h.address) + ": function is shorter than 12 bytes");
  }
  return true;
}

bool patch_hooks(std::vector<uint8_t> *data, const std::vector<std::vector<uint8_t>> &cmds,
                 const std::vector<ConvertHook> &hooks, std::string *error) {
  SegmentInfo text, dseg;
  if (!segment_info(cmds, "__TEXT", &text) || !segment_info(cmds, "__DATA", &dseg))
    return fail(error, "no __TEXT or __DATA segment");
  uint64_t end = dseg.vmaddr;
  for (const auto &s : dseg.sections) end = std::max(end, s.first + s.second);
  uint64_t table = (end + 15) & ~uint64_t(15);
  uint64_t room = (dseg.vmaddr + dseg.vmsize - table) / 8;
  if (hooks.size() > room)
    return fail(error, "hook table full: " + std::to_string(hooks.size()) + " hooks, room for " + std::to_string(room));
  if (!check_hooks(*data, hooks, text, error)) return false;
  for (size_t i = 0; i < hooks.size(); i++) {
    uint64_t addr = hooks[i].address, slot = table + 8 * i;
    int64_t pages = static_cast<int64_t>(slot >> 12) - static_cast<int64_t>(addr >> 12);
    uint32_t adrp = 0x90000000u | (uint32_t(pages & 3) << 29) | (uint32_t((pages >> 2) & 0x7FFFF) << 5) | 16;
    uint32_t ldr = 0xF9400000u | (uint32_t((slot & 0xFFF) / 8) << 10) | (16 << 5) | 16;
    uint32_t br = 0xD61F0200u;
    uint8_t *p = &(*data)[addr - text.vmaddr + text.fileoff];
    put32(p, adrp);
    put32(p + 4, ldr);
    put32(p + 8, br);
  }
  return true;
}

}  // namespace

bool thin_arm64(const std::vector<uint8_t> &in, std::vector<uint8_t> *out, std::string *error) {
  if (in.size() < 8) return fail(error, "not a Mach-O (truncated)");
  uint32_t magic = be32(in.data());
  if (magic != 0xCAFEBABE && magic != 0xCAFEBABF) {
    *out = in;
    return true;
  }
  bool wide = magic == 0xCAFEBABF;
  uint32_t n = be32(&in[4]);
  size_t entry = wide ? 32 : 20;
  if (8 + size_t(n) * entry > in.size()) return fail(error, "malformed universal binary (truncated)");
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *a = &in[8 + i * entry];
    if (be32(a) != kCpuArm64) continue;
    uint64_t off = wide ? be64(a + 8) : be32(a + 8), size = wide ? be64(a + 16) : be32(a + 12);
    if (off > in.size() || size > in.size() - off) return fail(error, "malformed universal binary (slice past the end)");
    out->assign(in.begin() + off, in.begin() + off + size);
    return true;
  }
  return fail(error, "no arm64 slice in the universal binary");
}

bool convert_executable(const std::vector<uint8_t> &in, const std::vector<ConvertHook> &hooks,
                        std::vector<uint8_t> *out, std::string *error) {
  std::vector<uint8_t> data = in;
  if (data.size() < 32) return fail(error, "not a thin arm64 Mach-O (truncated)");
  uint32_t magic = u32(&data[0]), cpu = u32(&data[4]), ftype = u32(&data[12]);
  uint32_t ncmds = u32(&data[16]), sizeofcmds = u32(&data[20]), flags = u32(&data[24]);
  if (magic != kMagic64 || cpu != kCpuArm64) return fail(error, "not a thin arm64 Mach-O");
  if (ftype != kExecute) return fail(error, "not an executable (already converted?)");
  size_t end = 32 + size_t(sizeofcmds);
  if (end > data.size()) return fail(error, "malformed load commands (sizeofcmds past the end of the file)");
  std::vector<std::vector<uint8_t>> cmds;
  size_t off = 32;
  for (uint32_t i = 0; i < ncmds; i++) {
    if (off + 8 > end) return fail(error, "malformed load commands (more commands than sizeofcmds holds)");
    uint32_t cmd = u32(&data[off]), size = u32(&data[off + 4]);
    if (size < 8 || off + size > end)
      return fail(error, "malformed load commands (bad cmdsize " + std::to_string(size) + " at offset " + std::to_string(off) + ")");
    if (cmd == kSegment64 && (size < 72 || 72 + 80 * size_t(u32(&data[off + 64])) > size))
      return fail(error, "malformed load commands (segment sections past cmdsize)");
    cmds.emplace_back(data.begin() + off, data.begin() + off + size);
    off += size;
  }
  SegmentInfo text;
  if (!segment_info(cmds, "__TEXT", &text)) return fail(error, "no __TEXT segment");

  size_t first_data = data.size();
  std::vector<std::vector<uint8_t>> result{dylib_command(kIdDylib, kImageId)};
  bool tagged = false;
  for (std::vector<uint8_t> c : cmds) {
    uint32_t cmd = u32(&c[0]);
    if ((cmd == kEncryptionInfo || cmd == kEncryptionInfo64) && c.size() >= 20 && u32(&c[16]))
      return fail(error, "encrypted (cryptid " + std::to_string(u32(&c[16])) + "): decrypt the game first");
    if (cmd == kMain || cmd == kLoadDylinker || cmd == kEncryptionInfo || cmd == kEncryptionInfo64) continue;
    if (is_version_min(cmd) || cmd == kBuildVersion) {
      if (!tagged) {  // one macOS tag, however many platform commands the input has
        std::vector<uint8_t> v(24, 0);
        put32(&v[0], kBuildVersion);
        put32(&v[4], 24);
        put32(&v[8], kPlatformMacOS);
        put32(&v[12], kMacOS11);
        put32(&v[16], kMacOS11);
        result.push_back(v);
        tagged = true;
      }
      continue;
    }
    if (is_dylib_load(cmd)) {
      if (c.size() < 24) return fail(error, "malformed load commands (dylib command shorter than 24 bytes)");
      uint32_t name_off = u32(&c[8]);
      if (name_off >= c.size()) return fail(error, "malformed load commands (dylib name past cmdsize)");
      const char *p = reinterpret_cast<const char *>(&c[name_off]);
      std::string lib = short_name(std::string(p, strnlen(p, c.size() - name_off)));
      bool host = lib == "libSystem" || lib == "libc++" || lib == "libz";
      result.push_back(host ? c : dylib_command(cmd, "@rpath/mcfm_stub_" + lib + ".dylib"));
      continue;
    }
    if (cmd == kSegment64) {
      if (segment_name(c) == "__PAGEZERO") {  // keep the slot: fixup opcodes address segments by index
        std::memset(&c[8], 0, 16);
        std::memcpy(&c[8], "__MCFM_PAD", 10);
        put64(&c[24], text.vmaddr - kPadSize);
        put64(&c[32], kPadSize);
      }
      uint32_t n = u32(&c[64]);
      for (uint32_t k = 0; k < n; k++) {
        size_t s = 72 + 80 * size_t(k);
        if (std::memcmp(&c[s], "__objc_", 7) == 0) c[s + 2] = 'x';  // hide ObjC metadata from libobjc
        uint32_t sect_off = u32(&c[s + 48]);
        uint64_t sect_size = u64(&c[s + 40]);
        if (sect_off && sect_size) first_data = std::min(first_data, size_t(sect_off));
      }
    }
    result.push_back(c);
  }

  std::vector<uint8_t> blob;
  for (const auto &c : result) blob.insert(blob.end(), c.begin(), c.end());
  if (32 + blob.size() > first_data)
    return fail(error, "not enough header padding (" + std::to_string(blob.size()) + " bytes of load commands, " +
                           std::to_string(first_data - 32) + " available)");
  if (!hooks.empty() && !patch_hooks(&data, cmds, hooks, error)) return false;
  size_t clear = std::max(size_t(sizeofcmds), blob.size());
  std::fill(data.begin() + 32, data.begin() + 32 + clear, 0);
  std::copy(blob.begin(), blob.end(), data.begin() + 32);
  put32(&data[12], kDylib);
  put32(&data[16], static_cast<uint32_t>(result.size()));
  put32(&data[20], static_cast<uint32_t>(blob.size()));
  put32(&data[24], flags & ~kPie);
  out->swap(data);
  return true;
}

}  // namespace loader
}  // namespace mcfm
