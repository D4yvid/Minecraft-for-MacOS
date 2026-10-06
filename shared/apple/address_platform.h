#pragma once
// mcfm::Platform for the stripped Apple (iOS / Mac Catalyst) build of 0.15.10: engine
// locations come from an IDA address table, used only after the image's LC_UUID matches.
#include <mcfm/platform.h>

namespace mcfm {
namespace apple {

class AddressPlatform : public Platform {
 public:
  AddressPlatform() : slide_(0), attached_(false) {}

  // Finds the game image by LC_UUID and checks the patched vtable slots hold the expected
  // functions. false: not the supported build — use nothing else on this object.
  bool attach();

  void log(const char *msg);
  bool patch_slot(engine::Slot slot, void *replacement, void **original);
  void *global(engine::Global g);
  void mouse_feed(int btn, int state, int x, int y, int dx, int dy);

  // Unslid address -> runtime address (for platform glue that needs other engine symbols).
  uintptr_t at(uintptr_t unslid) const { return unslid + slide_; }

 private:
  intptr_t slide_;
  bool attached_;
};

}  // namespace apple
}  // namespace mcfm
