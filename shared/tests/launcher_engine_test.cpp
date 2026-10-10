// Engine boot order with fake engine functions; the Xbox config seam replacement.
#include "engine.h"
#include "seams.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <functional>
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
static void *lifecycle_platform = nullptr;
static void fire_suspended(void *p) { calls.push_back("suspended"); lifecycle_platform = p; }
static void fire_resumed(void *) { calls.push_back("resumed"); }
static void fire_focus_lost(void *) { calls.push_back("focus_lost"); }
static void fire_focus_gained(void *) { calls.push_back("focus_gained"); }

int main() {
  for (auto &p : base_vtable) p = reinterpret_cast<void *>(&app_update);
  app_vtable[19] = reinterpret_cast<void *>(&app_update);
  app_vtable[20] = reinterpret_cast<void *>(&app_set_size_and_scale);
  app_vtable[21] = reinterpret_cast<void *>(&app_set_size);
  {  // AppKit may resize the window before the engine has started: no crash, nothing called.
    Engine early;
    early.resize(10, 10);
    early.frame();
    early.suspend();
    early.focus_lost();
    EXPECT(calls.empty());
  }
  EngineAddresses a;
  a.platform_ctor = reinterpret_cast<uintptr_t>(&platform_ctor);
  a.base_vtable = reinterpret_cast<uintptr_t>(base_vtable);
  a.client_ctor = reinterpret_cast<uintptr_t>(&client_ctor);
  a.app_init = reinterpret_cast<uintptr_t>(&app_init);
  a.graphics_vendor = a.graphics_renderer = a.graphics_version = a.graphics_extensions = reinterpret_cast<uintptr_t>(&gfx);
  a.fire_suspended = reinterpret_cast<uintptr_t>(&fire_suspended);
  a.fire_resumed = reinterpret_cast<uintptr_t>(&fire_resumed);
  a.fire_focus_lost = reinterpret_cast<uintptr_t>(&fire_focus_lost);
  a.fire_focus_gained = reinterpret_cast<uintptr_t>(&fire_focus_gained);
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
  // App lifecycle, as the iOS app delegate drives it: AppPlatform's notifiers on our platform
  // (suspended = the save on quit, focus lost/gained = pause on Cmd-Tab).
  calls.clear();
  engine.focus_lost();
  engine.focus_gained();
  engine.suspend();
  engine.resume();
  EXPECT(calls.size() == 4 && calls[0] == "focus_lost" && calls[1] == "focus_gained" && calls[2] == "suspended" && calls[3] == "resumed");
  EXPECT(lifecycle_platform == engine.platform());

  EngineAddresses s = EngineAddresses::for_slide(0x1000);
  EXPECT(s.platform_ctor == 0x10045F678 + 0x1000 && s.app_init == 0x1000555BC + 0x1000);
  EXPECT(s.fire_suspended == 0x100460280 + 0x1000 && s.fire_resumed == 0x10046038C + 0x1000);
  EXPECT(s.fire_focus_lost == 0x100460484 + 0x1000 && s.fire_focus_gained == 0x100460500 + 0x1000);

  size_t n = 0;
  const Hook *h = hooks(&n);
  EXPECT(n >= 1 && std::strcmp(h[0].name, "xbl_app_config") == 0 && h[0].address == 0x100798B34);
  // Called through the real library types (the engine's signatures), so libc++'s own layout
  // and destructors check the ABI and the ownership.
  {
    std::shared_ptr<void> c1 = reinterpret_cast<std::shared_ptr<void> (*)()>(h[0].replacement)();
    std::shared_ptr<void> c2 = reinterpret_cast<std::shared_ptr<void> (*)()>(h[0].replacement)();
    EXPECT(c1.get() != nullptr && c1.get() == c2.get() && c1.use_count() == 0);  // no control block
  }
  // Seam #2: StoreFactory::createStores returns one null store (no Objective-C, licensed).
  const Hook *store_hook = nullptr;
  for (size_t i = 0; i < n; i++) if (std::strcmp(h[i].name, "create_stores") == 0) store_hook = &h[i];
  EXPECT(store_hook && store_hook->address == 0x100711774);
  if (store_hook) {
    static bool initialized_called = false, initialized_value = true;
    struct Listener { static void on_initialized(void *, bool ok) { initialized_called = true; initialized_value = ok; } };
    void *listener_vtable[3] = {nullptr, nullptr, reinterpret_cast<void *>(&Listener::on_initialized)};
    void *listener = listener_vtable;
    void *listener_obj = &listener;
    // std::vector<std::unique_ptr<Store>> has the layout of std::vector<void *>; its destructor
    // frees the buffer with the engine's operator delete.
    std::vector<void *> v = reinterpret_cast<std::vector<void *> (*)(void *, void *)>(store_hook->replacement)(nullptr, listener_obj);
    EXPECT(v.size() == 1);
    EXPECT(initialized_called && !initialized_value);
    void *store = v.empty() ? nullptr : v[0];
    if (store) {
      void **svt = *static_cast<void ***>(store);
      EXPECT(reinterpret_cast<intptr_t>(svt[-2]) == 0 && svt[-1] == nullptr);  // offset-to-top, RTTI
      EXPECT(reinterpret_cast<void *(*)(void *)>(svt[0])(store) == store);     // ~Store() returns this
      EXPECT(!reinterpret_cast<bool (*)(void *)>(svt[2])(store));    // requiresRestorePurchasesButton
      EXPECT(!reinterpret_cast<bool (*)(void *)>(svt[3])(store));    // allowsSubscriptions
      EXPECT(reinterpret_cast<std::string (*)(void *)>(svt[4])(store) == "mcfm-null");  // getStoreId
      std::vector<std::string> ids(1, "x");
      reinterpret_cast<void (*)(void *, const std::vector<std::string> &)>(svt[5])(store, ids);  // queryProducts
      const std::string product = "p", payload = "";
      reinterpret_cast<void (*)(void *, const std::string &, int, const std::string &)>(svt[6])(store, product, 0, payload);
      reinterpret_cast<void (*)(void *, const void *, int)>(svt[7])(store, &product, 0);  // acknowledgePurchase
      reinterpret_cast<void (*)(void *)>(svt[8])(store);             // queryPurchases
      reinterpret_cast<void (*)(void *)>(svt[9])(store);             // restorePurchases
      EXPECT(!reinterpret_cast<bool (*)(void *)>(svt[10])(store));   // isTrial
      reinterpret_cast<void (*)(void *)>(svt[11])(store);            // purchaseGame
      EXPECT(reinterpret_cast<bool (*)(void *)>(svt[12])(store));    // isGameLicensed
      EXPECT(reinterpret_cast<std::string (*)(void *)>(svt[13])(store).empty());  // getAppReceipt
      reinterpret_cast<void (*)(void *, std::function<void()>)>(svt[14])(store, [] {});  // registerLicenseChangeCallback
      reinterpret_cast<void (*)(void *)>(svt[15])(store);            // handleLicenseChange
      reinterpret_cast<void (*)(void *)>(svt[1])(store);             // deleting destructor (unique_ptr)
    }
  }
  // Seam #1: the only engine use of the iOS HTTP glue is the telemetry event-batch upload,
  // which is dropped: the hook is a no-op.
  const Hook *upload = nullptr;
  for (size_t i = 0; i < n; i++) if (std::strcmp(h[i].name, "telemetry_upload") == 0) upload = &h[i];
  EXPECT(upload && upload->address == 0x1003B42A8);
  if (upload) reinterpret_cast<void (*)(void *, void *)>(upload->replacement)(nullptr, nullptr);
  // Xbox Live is dropped: MinecraftClient::init must not push the first-launch prompt screen.
  const Hook *prompt = nullptr;
  for (size_t i = 0; i < n; i++) if (std::strcmp(h[i].name, "xbl_first_launch_screen") == 0) prompt = &h[i];
  EXPECT(prompt && prompt->address == 0x100158624);
  if (prompt) reinterpret_cast<void (*)(void *)>(prompt->replacement)(nullptr);  // no-op
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_engine_test: all passed\n");
  return 0;
}
