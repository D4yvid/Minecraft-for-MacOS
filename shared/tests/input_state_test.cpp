#include <mcfm/input/input_state.h>
#include <cstdio>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  using namespace mcfm;
  // held keys/buttons: only real transitions pass, release_all frees everything once
  HeldSet held;
  EXPECT(held.set(87, true));
  EXPECT(!held.set(87, true));          // repeat press ignored
  EXPECT(held.set(1, true));            // mouse button 1 shares the table safely (own instance in app)
  int released[4] = {0}, n = 0;
  held.release_all([&](int code) { if (n < 4) released[n++] = code; });
  EXPECT(n == 2 && released[0] == 1 && released[1] == 87);
  EXPECT(!held.set(87, false));         // already released
  n = 0;
  held.release_all([&](int) { n++; });
  EXPECT(n == 0);

  // while typing only Esc reaches the engine (closes chat / text entry)
  EXPECT(passes_while_typing(27));
  EXPECT(!passes_while_typing('W'));
  EXPECT(!passes_while_typing('E'));
  EXPECT(!passes_while_typing(13));

  // look deltas keep their fractional part instead of truncating toward zero
  DeltaAccumulator look;
  int ox = 0, oy = 0, sx = 0, sy = 0;
  for (int i = 0; i < 10; i++) { look.add(0.3f, -0.3f, &ox, &oy); sx += ox; sy += oy; }
  EXPECT(sx == 3 && sy == -3);
  look.add(2.6f, 0.0f, &ox, &oy);
  EXPECT(ox == 2 || ox == 3);

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("input_state_test: all passed\n");
  return 0;
}
