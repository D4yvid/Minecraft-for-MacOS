// The launcher's hooks as mcfm_image.py dylib --hooks reads them (name, unslid address), for the
// iOS app's build (ios/tools/build_app.sh): the same list mcfm-launch --print-hooks prints.
#include <cstdio>

#include "seams.h"

int main() {
  size_t n = 0;
  const mcfm::launcher::Hook *h = mcfm::launcher::hooks(&n);
  for (size_t i = 0; i < n; i++) std::printf("%s\t0x%lx\n", h[i].name, static_cast<unsigned long>(h[i].address));
  return 0;
}
