#include "game_import.h"

#include <cstdio>
#include <fstream>
#include <iterator>

#include "macho_uuid.h"

namespace mcfm {
namespace launcher {

std::string import_game(const std::string &binary_path, const std::string &dir, const ImportOptions &options) {
  std::ifstream f(binary_path, std::ios::binary);
  if (!f) return "Cannot read the game binary (" + binary_path + ").";
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::vector<uint8_t> thin, image;
  std::string error;
  if (!loader::thin_arm64(file, &thin, &error)) return "Not a usable game binary: " + error + ".";
  if (options.check_game_uuid && !is_expected_game_image(thin.data(), thin.size()))
    return "This is not Minecraft PE 0.15.10 for iOS (the build this launcher supports).";
  if (!loader::convert_executable(thin, options.hooks, &image, &error)) return "Cannot convert the game: " + error + ".";
  // Written next to the image, then renamed over it: a reader sees the old or the new image.
  std::string tmp = dir + "/minecraftpe.dylib.tmp", final_path = dir + "/minecraftpe.dylib";
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    o.flush();
    if (!o) {
      std::remove(tmp.c_str());
      return "Cannot write the converted game (is the storage full?).";
    }
  }
  if (std::rename(tmp.c_str(), final_path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return "Cannot replace the previous game image.";
  }
  return std::string();
}

}  // namespace launcher
}  // namespace mcfm
