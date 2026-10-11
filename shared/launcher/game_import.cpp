#include "game_import.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>

#include "macho_uuid.h"

namespace mcfm {
namespace launcher {
namespace {

std::string read_file(const std::string &path, bool *ok) {
  std::ifstream f(path, std::ios::binary);
  *ok = static_cast<bool>(f);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string hooks_text(const std::vector<loader::ConvertHook> &hooks) {
  std::string out;
  for (const loader::ConvertHook &h : hooks) {
    char address[32];
    std::snprintf(address, sizeof address, "%llx", static_cast<unsigned long long>(h.address));
    out += h.name + "\t" + address + "\n";
  }
  return out;
}

// Written next to `path`, flushed to the storage, then renamed over it: a reader sees the old or
// the new file, also after a power loss.
bool replace_file(const std::string &path, const void *data, size_t size) {
  std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const char *p = static_cast<const char *>(data);
  bool ok = true;
  for (size_t done = 0; ok && done < size;) {
    ssize_t n = write(fd, p + done, size - done);
    if (n > 0) done += static_cast<size_t>(n);
    else ok = false;
  }
  ok = ok && fsync(fd) == 0;
  ok = close(fd) == 0 && ok;
  if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
  if (!ok) std::remove(tmp.c_str());
  return ok;
}

}  // namespace

bool image_is_current(const std::string &dir, const std::vector<loader::ConvertHook> &hooks) {
  bool image = false, recorded = false;
  read_file(dir + "/minecraftpe.dylib", &image).clear();
  std::string text = read_file(dir + "/minecraftpe.hooks", &recorded);
  return image && recorded && text == hooks_text(hooks);
}

std::string import_game(const std::string &binary_path, const std::string &dir, const ImportOptions &options) {
  bool readable = false;
  std::string bytes = read_file(binary_path, &readable);
  if (!readable) return "Cannot read the game binary (" + binary_path + ").";
  std::vector<uint8_t> file(bytes.begin(), bytes.end()), thin, image;
  bytes.clear();
  std::string error;
  if (!loader::thin_arm64(file, &thin, &error)) return "Not a usable game binary: " + error + ".";
  if (options.check_game_uuid && !is_expected_game_image(thin.data(), thin.size()))
    return "This is not Minecraft PE 0.15.10 for iOS (the build this launcher supports).";
  if (!loader::convert_executable(thin, options.hooks, &image, &error)) return "Cannot convert the game: " + error + ".";
  // The image first: a crash before the hooks file leaves a stale record, which only means
  // converting again on the next start.
  if (!replace_file(dir + "/minecraftpe.dylib", image.data(), image.size()))
    return "Cannot write the converted game (is the storage full?).";
  std::string hooks = hooks_text(options.hooks);
  if (!replace_file(dir + "/minecraftpe.hooks", hooks.data(), hooks.size()))
    return "Cannot write the converted game (is the storage full?).";
  return std::string();
}

}  // namespace launcher
}  // namespace mcfm
