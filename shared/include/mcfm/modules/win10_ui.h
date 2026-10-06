#pragma once
// Win10 (desktop) GUI: makes AppPlatform report the "win10" edition, desktop UI scaling
// and a centred GUI while `enabled()` is true; otherwise the platform's own values. C++11.
#include <mcfm/module.h>

namespace mcfm {

class Win10UiModule : public Module {
 public:
  typedef bool (*EnabledFn)();
  explicit Win10UiModule(EnabledFn enabled);
  const char *name() const { return "win10_ui"; }
  // Fails only when GetEdition cannot be patched; other missing slots are logged.
  bool init(Platform &platform);
};

}  // namespace mcfm
