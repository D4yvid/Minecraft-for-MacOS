// macOS (Mac Catalyst) entry point: the shared features on the Apple address platform
// plus the Mac-only glue (input, pointer capture, resizing, title bar).
#include <mcfm/keyboard_mouse.h>
#include <mcfm/win10_ui.h>
#include "address_platform.h"
#include "mac_input.h"
#include "pointer_lock.h"
#include "resize.h"
#include "store.h"

namespace {

void capture_pointer() { pl::set_wanted(true); }
void release_pointer() { pl::set_wanted(false); }

}  // namespace

__attribute__((constructor)) static void mcfm_macos_init() {
  // Function-local static: a load-time constructor may run before this image's C++
  // globals are initialised, so nothing here may rely on a namespace-scope object.
  static mcfm::apple::AddressPlatform platform;
  if (!platform.attach()) {
    platform.log("unexpected binary, disabled");
    return;
  }
  mcfm::keyboard_mouse::PointerCallbacks pointer = {&capture_pointer, &release_pointer};
  bool ok = mcfm::win10_ui::install(platform);
  ok = mcfm::keyboard_mouse::install(platform, pointer) && ok;
  pl::install();
  resize::install();
  macin::install();
  store::install();
  platform.log(ok ? "patched" : "patched (some features failed)");
}
