#pragma once
// The skin screen's image picker on the Mac (AppPlatform slot 25 pickImage, see
// shared/launcher/app_platform.h): the picture the open panel returns becomes a PNG in the
// game's temp dir, and the answer waits here until the frame loop hands it to the engine.
#include <mutex>
#include <string>

namespace mcfm {
namespace launcher {

// Decodes any picture ImageIO reads (its pixels, whatever its DPI) and writes it as a PNG at
// `png_path` (atomically; missing directories are created). False if it is not a picture.
bool write_png(const std::string &source_path, const std::string &png_path);

// The picker's answer, posted from any thread and taken on the engine's thread.
class PickMailbox {
 public:
  void post(const std::string &png_path) {  // "" = cancelled
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = png_path;
    full_ = true;
  }
  bool take(std::string *png_path) {  // true once per post
    std::lock_guard<std::mutex> lock(mutex_);
    if (!full_) return false;
    full_ = false;
    *png_path = path_;
    return true;
  }

 private:
  std::mutex mutex_;
  bool full_ = false;
  std::string path_;
};

}  // namespace launcher
}  // namespace mcfm
