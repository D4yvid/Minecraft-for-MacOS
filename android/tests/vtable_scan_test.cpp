// Host test for the Android vtable lookup (pure; no Android headers).
#include "../jni/vtable_scan.hpp"

#include <cstdio>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void f1() {}
static void f2() {}
static void f3() {}
static void g1() {}
static int typeinfo_a, typeinfo_b;

int main() {
  using mcfm::android::find_vtable_index;
  // Two vtables back to back, as in .data.rel.ro: {offset-to-top, typeinfo, functions...}
  void *mem[] = {0, &typeinfo_a, (void *)&f1, (void *)&f2, (void *)&f3,
                 0, &typeinfo_b, (void *)&g1};
  EXPECT(find_vtable_index(mem, (void *)&f1) == 2);
  EXPECT(find_vtable_index(mem, (void *)&f3) == 4);
  EXPECT(find_vtable_index(mem, (void *)&g1) == -1);  // belongs to the next vtable
  EXPECT(find_vtable_index(mem, 0) == -1);             // dlsym failed: never match offset-to-top
  EXPECT(find_vtable_index(mem, &typeinfo_a) == -1);   // header words are not slots
  EXPECT(find_vtable_index(0, (void *)&f1) == -1);     // vtable symbol missing
  void *longer[600];
  longer[0] = 0;
  longer[1] = &typeinfo_a;
  for (int i = 2; i < 600; i++) longer[i] = (void *)&f1;
  EXPECT(find_vtable_index(longer, (void *)&f2) == -1);  // bounded scan, no overrun
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("vtable_scan_test: all passed\n");
  return 0;
}
