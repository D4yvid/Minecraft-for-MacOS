// createUUID from several engine threads at once: no data race (built with ThreadSanitizer).
#include "app_platform.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace mcfm::launcher;

int main() {
  void *base[kBaseSlots] = {}, *vt[kBaseSlots];
  EngineFns fns = {nullptr, nullptr, nullptr, nullptr};
  build_vtable(vt, base, fns);
  auto uuid = reinterpret_cast<std::string (*)(void *)>(vt[81]);
  std::vector<std::thread> threads;
  int bad = 0;
  std::vector<int> bad_per(8, 0);
  for (int t = 0; t < 8; t++)
    threads.emplace_back([&, t] {
      for (int i = 0; i < 2000; i++)
        if (uuid(nullptr).size() != 36) bad_per[t]++;
    });
  for (auto &th : threads) th.join();
  for (int b : bad_per) bad += b;
  if (bad) { std::printf("%d malformed UUIDs\n", bad); return 1; }
  std::printf("launcher_uuid_race_test: all passed\n");
  return 0;
}
