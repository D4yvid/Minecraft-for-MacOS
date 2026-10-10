// Darwin's ctype entry points (the game's inline isalpha & co. read __DefaultRuneLocale, shared
// with our libc++, and call these above ASCII) and locale functions with Darwin's LC_* numbers.
// Only the C locale, as on the game's iOS build.
#include <locale.h>
#include <string.h>

#include <__config_site>

#include "darwin.h"

// uselocale/freelocale pass through: bionic's LC_GLOBAL_LOCALE is ((locale_t) -1L), as Darwin's.

namespace {

int to_bionic_category(int d) {
  if (d == darwin::kLC_ALL) return LC_ALL;
  if (d == darwin::kLC_COLLATE) return LC_COLLATE;
  if (d == darwin::kLC_CTYPE) return LC_CTYPE;
  if (d == darwin::kLC_MONETARY) return LC_MONETARY;
  if (d == darwin::kLC_NUMERIC) return LC_NUMERIC;
  if (d == darwin::kLC_TIME) return LC_TIME;
  if (d == darwin::kLC_MESSAGES) return LC_MESSAGES;
  return -1;
}

int to_bionic_mask(int d) {
  int b = 0;
  if (d & darwin::kLC_COLLATE_MASK) b |= LC_COLLATE_MASK;
  if (d & darwin::kLC_CTYPE_MASK) b |= LC_CTYPE_MASK;
  if (d & darwin::kLC_MONETARY_MASK) b |= LC_MONETARY_MASK;
  if (d & darwin::kLC_NUMERIC_MASK) b |= LC_NUMERIC_MASK;
  if (d & darwin::kLC_TIME_MASK) b |= LC_TIME_MASK;
  if (d & darwin::kLC_MESSAGES_MASK) b |= LC_MESSAGES_MASK;
  return b;
}

}  // namespace

extern "C" {

unsigned long mcfm_darwin___maskrune(int c, unsigned long mask) {
  // The C locale classifies only 0..255 (the table); nothing above.
  return c >= 0 && c < 256 ? mcfm_darwin_DefaultRuneLocale.__runetype[c] & mask : 0;
}

int mcfm_darwin___tolower(int c) { return c >= 0 && c < 256 ? mcfm_darwin_DefaultRuneLocale.__maplower[c] : c; }

int mcfm_darwin___toupper(int c) { return c >= 0 && c < 256 ? mcfm_darwin_DefaultRuneLocale.__mapupper[c] : c; }

// bionic's C locale is named "C.UTF-8"; Darwin's is "C".
char *mcfm_darwin_setlocale(int category, const char *name) {
  int b = to_bionic_category(category);
  if (b < 0) return nullptr;
  char *r = setlocale(b, name);
  if (r && (strcmp(r, "C.UTF-8") == 0 || strcmp(r, "POSIX") == 0)) return const_cast<char *>("C");
  return r;
}

locale_t mcfm_darwin_newlocale(int mask, const char *name, locale_t base) {
  return newlocale(to_bionic_mask(mask), name, base);
}

}  // extern "C"
