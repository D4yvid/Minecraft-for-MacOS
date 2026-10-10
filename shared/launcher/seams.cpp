#include "seams.h"

#include "addresses_0_15_10.h"

namespace mcfm {
namespace launcher {
namespace {

// std::shared_ptr<T> by value is returned through x8; a type with a user-provided destructor
// gets the same treatment, so this function has the original's ABI.
struct SharedPtrOut {
  void *ptr;
  void *ctrl;
  ~SharedPtrOut() {}
};

// Seam #3: the Xbox Live services config singleton reads xboxservices.config through
// NSBundle (crashes without Foundation). Xbox Live is dropped, so hand out one zeroed
// object: MinecraftClient::init only stores strings into it (zeroed = empty std::string).
SharedPtrOut xbl_app_config() {
  alignas(16) static char config[0x400];
  SharedPtrOut out;
  out.ptr = config;
  out.ctrl = nullptr;
  return out;
}

const Hook kHooks[] = {
    {"xbl_app_config", addr::kFnXblAppConfig, reinterpret_cast<void *>(&xbl_app_config)},
};

}  // namespace

const Hook *hooks(size_t *count) {
  *count = sizeof kHooks / sizeof kHooks[0];
  return kHooks;
}

}  // namespace launcher
}  // namespace mcfm
