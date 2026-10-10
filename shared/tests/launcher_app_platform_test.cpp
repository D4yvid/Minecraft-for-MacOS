// Our AppPlatform vtable: base slots kept, ours called with the engine's ABI.
#include "app_platform.h"

#include <cstdio>
#include <string>

using namespace mcfm::launcher;

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string fake_vendor(void *) { return "vendor"; }
static int base_fn_marker[kBaseSlots];

template <class R, class... A> R call(void **vt, int slot, A... a) {
  return reinterpret_cast<R (*)(void *, A...)>(vt[slot])(nullptr, a...);
}

int main() {
  void *base[kBaseSlots], *vt[kBaseSlots];
  for (size_t i = 0; i < kBaseSlots; i++) base[i] = &base_fn_marker[i];
  EngineFns fns = {reinterpret_cast<void *>(&fake_vendor), &base_fn_marker[1], &base_fn_marker[2], &base_fn_marker[3]};
  HostInfo info;
  info.data_dir = "/game/data/"; info.external_dir = "/s/ext"; info.internal_dir = "/s/int";
  info.userdata_dir = "/s/user"; info.temp_dir = "/s/tmp"; info.region = "en_US"; info.device_id = "dev-1";
  set_host_info(info);
  build_vtable(vt, base, fns);

  const int ours[] = {2, 4, 9, 10, 18, 20, 21, 22, 23, 24, 25, 33, 34, 35, 53, 66, 68, 69, 74, 80, 81, 82, 83, 84, 93, 96, 99, 100};
  for (size_t i = 0; i < kBaseSlots; i++) {
    bool mine = false;
    for (int s : ours) mine |= (s == static_cast<int>(i));
    if (!mine) EXPECT(vt[i] == base[i]);
  }
  EXPECT(call<std::string>(vt, 2) == "/game/data/");
  EXPECT(call<std::string>(vt, 4) == "/game/data/");
  EXPECT(call<std::string>(vt, 21) == "vendor");
  EXPECT(vt[22] == &base_fn_marker[1]);
  EXPECT((call<const std::string &>(vt, 20)) == "en_US");
  EXPECT((call<const std::string &>(vt, 33)) == "/s/ext");
  EXPECT((call<const std::string &>(vt, 34)) == "/s/int");
  EXPECT((call<const std::string &>(vt, 35)) == "/s/user");
  EXPECT((call<const std::string &>(vt, 100)) == "/s/tmp");
  const std::string rel = "lang/en_US.lang";
  EXPECT((call<std::string, const std::string &>(vt, 53, rel)) == "/game/data/lang/en_US.lang");
  EXPECT(call<bool>(vt, 66) && call<bool>(vt, 68));
  EXPECT(call<int>(vt, 69) == 0);
  EXPECT(call<std::string>(vt, 74) == "com.mojang.minecraftpe");
  EXPECT(call<std::string>(vt, 80) == "dev-1");
  std::string u = call<std::string>(vt, 81), v = call<std::string>(vt, 81);
  EXPECT(u.size() == 36 && u[8] == '-' && u[13] == '-' && u[18] == '-' && u[23] == '-' && u[14] == '4');
  EXPECT(u.find_first_not_of("0123456789abcdef-") == std::string::npos);
  EXPECT(u != v);
  EXPECT(!call<bool>(vt, 82) && !call<bool>(vt, 83) && !call<bool>(vt, 84));
  EXPECT(call<std::string>(vt, 93) == "win10");
  EXPECT(call<int>(vt, 96) == 1);
  EXPECT(call<int>(vt, 99) == 0);
  call<void>(vt, 18);
  call<void, void *>(vt, 25, nullptr);
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("launcher_app_platform_test: all passed\n");
  return 0;
}
