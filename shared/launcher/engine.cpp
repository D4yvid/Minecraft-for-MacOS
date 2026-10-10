#include "engine.h"

#include <cstdlib>

#include "addresses_0_15_10.h"

namespace mcfm {
namespace launcher {
namespace {

template <class F> F fn_at(uintptr_t address) { return reinterpret_cast<F>(address); }
void **vtable_of(void *object) { return *static_cast<void ***>(object); }

}  // namespace

EngineAddresses EngineAddresses::for_slide(uintptr_t slide) {
  EngineAddresses a;
  a.platform_ctor = addr::kFnAppPlatformCtor + slide;
  a.base_vtable = addr::kBaseAppPlatformVtable + slide;
  a.client_ctor = addr::kFnMinecraftClientCtor + slide;
  a.app_init = addr::kFnAppInit + slide;
  a.graphics_vendor = addr::kFnGraphicsVendor + slide;
  a.graphics_renderer = addr::kFnGraphicsRenderer + slide;
  a.graphics_version = addr::kFnGraphicsVersion + slide;
  a.graphics_extensions = addr::kFnGraphicsExtensions + slide;
  return a;
}

bool Engine::start(const EngineAddresses &a, const HostInfo &info, int width, int height) {
  set_host_info(info);
  // AppPlatform: the base constructor (sets the singleton), then our vtable.
  platform_ = std::calloc(1, addr::kAppPlatformSize);
  context_ = std::calloc(1, 16);  // AppContext is an empty object on iOS
  app_ = std::calloc(1, addr::kMinecraftClientSize);
  if (!platform_ || !context_ || !app_) return false;
  fn_at<void (*)(void *)>(a.platform_ctor)(platform_);
  EngineFns fns = {reinterpret_cast<void *>(a.graphics_vendor), reinterpret_cast<void *>(a.graphics_renderer),
                   reinterpret_cast<void *>(a.graphics_version), reinterpret_cast<void *>(a.graphics_extensions)};
  build_vtable(vtable_, reinterpret_cast<void *const *>(a.base_vtable), fns);
  *static_cast<void ***>(platform_) = vtable_;
  // MinecraftClient(argc, argv), App::init(AppContext&), then the sizes (initView).
  fn_at<void (*)(void *, int, char **)>(a.client_ctor)(app_, 0, nullptr);
  fn_at<void (*)(void *, void *)>(a.app_init)(app_, context_);
  resize(width, height);
  return true;
}

void Engine::frame() {
  reinterpret_cast<void (*)(void *)>(vtable_of(app_)[addr::kAppSlotUpdate])(app_);
}

void Engine::resize(int width, int height) {
  reinterpret_cast<void (*)(void *, int, int)>(vtable_of(app_)[addr::kAppSlotSetSize])(app_, width, height);
  reinterpret_cast<void (*)(void *, int, int, float)>(vtable_of(app_)[addr::kAppSlotSetSizeAndScale])(app_, width, height, 0.f);
}

}  // namespace launcher
}  // namespace mcfm
