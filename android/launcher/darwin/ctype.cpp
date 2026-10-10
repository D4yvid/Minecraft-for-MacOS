// Darwin's C-locale _RuneLocale (from the generated darwin_ctype.inc): the object the game
// imports as __DefaultRuneLocale and our libc++'s ctype<char> reads (runtime/include/__config_site).
#include <stddef.h>
#include <string.h>

#include <__config_site>

#include "darwin.h"

#include "darwin_ctype.inc"

static_assert(sizeof(mcfm_darwin_RuneLocale) == darwin::kSizeof_RuneLocale, "_RuneLocale size");
static_assert(sizeof(mbstate_t) == darwin::kSizeof_mbstate_t, "mbstate_t is Darwin's (__config_site)");
static_assert(offsetof(mcfm_darwin_RuneLocale, __runetype) == darwin::kOffsetof_RuneLocale___runetype, "__runetype");
static_assert(offsetof(mcfm_darwin_RuneLocale, __maplower) == darwin::kOffsetof_RuneLocale___maplower, "__maplower");
static_assert(offsetof(mcfm_darwin_RuneLocale, __mapupper) == darwin::kOffsetof_RuneLocale___mapupper, "__mapupper");

namespace {
mcfm_darwin_RuneLocale make() {
  mcfm_darwin_RuneLocale r;
  memset(&r, 0, sizeof r);
  memcpy(r.__magic, "RuneMagA", 8);
  memcpy(r.__encoding, "NONE", 4);
  r.__invalid_rune = 0xFFFD;
  memcpy(r.__runetype, kDarwin_runetype, sizeof r.__runetype);
  memcpy(r.__maplower, kDarwin_maplower, sizeof r.__maplower);
  memcpy(r.__mapupper, kDarwin_mapupper, sizeof r.__mapupper);
  return r;
}
}  // namespace

// Constant-initialized would be better, but the tables are plain arrays: initialize before any
// other runtime code (init_priority 101) so libc++'s own static initializers see it filled.
extern "C" const mcfm_darwin_RuneLocale mcfm_darwin_DefaultRuneLocale __attribute__((init_priority(101))) = make();
