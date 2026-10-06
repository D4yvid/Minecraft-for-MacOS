// iOS entry point: the hardcoded Win10 (desktop) UI on the Apple address platform, and no
// App Store receipt prompt. Touch input stays as the game ships it.
#include <mcfm/win10_ui.h>
#include "address_platform.h"
#include "store.h"

__attribute__((constructor)) static void mcfm_ios_init() {
  // Function-local static: a load-time constructor may run before this image's C++
  // globals are initialised, so nothing here may rely on a namespace-scope object.
  static mcfm::apple::AddressPlatform platform;
  if (!platform.attach()) {
    platform.log("unexpected binary, disabled");
    return;
  }
  bool ok = mcfm::win10_ui::install(platform);
  store::install();
  platform.log(ok ? "patched" : "patched (some features failed)");
}
