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

  // Releasing Cmd frees keys whose key-up AppKit swallowed while Cmd was held; modifiers stay.
  EXPECT(is_modifier_vk(16) && is_modifier_vk(17) && is_modifier_vk(18) && is_modifier_vk(91));
  EXPECT(!is_modifier_vk('W') && !is_modifier_vk(27));
  HeldSet cmd;
  cmd.set('W', true);
  cmd.set(16, true);
  int freed = 0, freed_key = 0;
  cmd.release_if([](int c) { return !is_modifier_vk(c); }, [&](int c) { freed++; freed_key = c; });
  EXPECT(freed == 1 && freed_key == 'W' && !cmd.held['W'] && cmd.held[16]);
  EXPECT(cmd.set('W', true));  // the next real press goes through again

  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("input_state_test: all passed\n");
  return 0;
}
