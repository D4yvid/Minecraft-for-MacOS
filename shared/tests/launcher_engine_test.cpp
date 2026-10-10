// Engine boot order with fake engine functions; the Xbox config seam replacement.
#include "engine.h"
#include "seams.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<std::string> calls;
static void *base_vtable[kBaseSlots];
static void *seen_platform_vptr = nullptr, *init_ctx = nullptr;
static int last_w, last_h;

static void platform_ctor(void *self) { calls.push_back("platform_ctor"); *static_cast<void **>(self) = base_vtable; }
static void app_update(void *) { calls.push_back("update"); }
static void app_set_size_and_scale(void *, int w, int h, float s) { calls.push_back("ui_size"); last_w = w; last_h = h; EXPECT(s == 0.f); }
static void app_set_size(void *, int w, int h) { calls.push_back("size"); last_w = w; last_h = h; }
static void *app_vtable[22];
static void client_ctor(void *self, int argc, char **argv) {
  calls.push_back("client_ctor");
  EXPECT(argc == 0 && argv == nullptr);
  seen_platform_vptr = nullptr;
  *static_cast<void **>(self) = app_vtable;
}
static void app_init(void *, void *ctx) { calls.push_back("init"); init_ctx = ctx; }
static std::string gfx(void *) { return "gfx"; }

int main() {
  for (auto &p : base_vtable) p = reinterpret_cast<void *>(&app_update);
  app_vtable[19] = reinterpret_cast<void *>(&app_update);
  app_vtable[20] = reinterpret_cast<void *>(&app_set_size_and_scale);
  app_vtable[21] = reinterpret_cast<void *>(&app_set_size);
  EngineAddresses a;
  a.platform_ctor = reinterpret_cast<uintptr_t>(&platform_ctor);
  a.base_vtable = reinterpret_cast<uintptr_t>(base_vtable);
  a.client_ctor = reinterpret_cast<uintptr_t>(&client_ctor);
  a.app_init = reinterpret_cast<uintptr_t>(&app_init);
  a.graphics_vendor = a.graphics_renderer = a.graphics_version = a.graphics_extensions = reinterpret_cast<uintptr_t>(&gfx);
  HostInfo info;
  info.data_dir = "/d/";
  Engine engine;
  EXPECT(engine.start(a, info, 1280, 720));
  const char *want[] = {"platform_ctor", "client_ctor", "init", "size", "ui_size"};
  EXPECT(calls.size() == 5);
  for (size_t i = 0; i < 5 && i < calls.size(); i++) EXPECT(calls[i] == want[i]);
  EXPECT(last_w == 1280 && last_h == 720);
  EXPECT(init_ctx != nullptr);
  void **vt = *static_cast<void ***>(engine.platform());
  EXPECT(vt != base_vtable);  // ours, not the base one
  EXPECT(reinterpret_cast<std::string (*)(void *)>(vt[2])(nullptr) == "/d/");
  calls.clear();
  engine.frame();
  engine.resize(800, 600);
  EXPECT(calls.size() == 3 && calls[0] == "update" && calls[1] == "size" && calls[2] == "ui_size");
  EXPECT(last_w == 800 && last_h == 600);

  EngineAddresses s = EngineAddresses::for_slide(0x1000);
  EXPECT(s.platform_ctor == 0x10045F678 + 0x1000 && s.app_init == 0x1000555BC + 0x1000);

  size_t n = 0;
  const Hook *h = hooks(&n);
  EXPECT(n >= 1 && std::strcmp(h[0].name, "xbl_app_config") == 0 && h[0].address == 0x100798B34);
  struct SharedPtrOut { void *ptr; void *ctrl; ~SharedPtrOut() {} };  // engine's std::shared_ptr ABI
  SharedPtrOut c1 = reinterpret_cast<SharedPtrOut (*)()>(h[0].replacement)();
  SharedPtrOut c2 = reinterpret_cast<SharedPtrOut (*)()>(h[0].replacement)();
  EXPECT(c1.ptr != nullptr && c1.ctrl == nullptr && c1.ptr == c2.ptr);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_engine_test: all passed\n");
  return 0;
}
