#pragma once
// Win10 (desktop) GUI, hardcoded as in the macOS mod: AppPlatform reports the "win10"
// edition, desktop UI scaling and a centred GUI. C++11.
#include <mcfm/platform.h>

namespace mcfm {
namespace win10_ui {

// false when GetEdition cannot be patched; other missing slots are logged and skipped.
bool install(Platform &platform);

}  // namespace win10_ui
}  // namespace mcfm
