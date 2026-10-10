#include "stub_runtime.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Called with `lock` held. `selector` NULL: a plain symbol line.
static void record(const char *lib, const char *symbol, const char *selector) {
  char line[512];
  if (selector)
    snprintf(line, sizeof line, "%s:%s %s", lib, symbol, selector);
  else
    snprintf(line, sizeof line, "%s:%s", lib, symbol);
  fprintf(stderr, "mcfm: stub %s\n", line);
  const char *path = getenv("MCFM_CENSUS");
  if (path && *path) {
    FILE *f = fopen(path, "a");
    if (f) {
      fprintf(f, "%s\n", line);
      fclose(f);
    }
  }
}

void mcfm_stub_hit(int *seen, const char *lib, const char *symbol) {
  if (__atomic_load_n(seen, __ATOMIC_ACQUIRE)) return;  // logged already: no lock on hot paths
  pthread_mutex_lock(&lock);
  if (!*seen) {
    record(lib, symbol, 0);
    __atomic_store_n(seen, 1, __ATOMIC_RELEASE);
  }
  pthread_mutex_unlock(&lock);
}

// Selectors already logged, keyed by pointer (each selector name is one string in the image).
// At most SEL_LOG_MAX are logged, so the open-addressing table never gets more than half full
// and a lookup stays short however many selectors the game sends.
#define SEL_SLOTS 8192
#define SEL_LOG_MAX (SEL_SLOTS / 2)
static const char *logged_sels[SEL_SLOTS];
static int logged_count;
static int logged_null;

void mcfm_stub_msgsend(const char *lib, const char *symbol, const char *selector) {
  pthread_mutex_lock(&lock);
  if (logged_count < SEL_LOG_MAX) {
    if (!selector) {
      if (!logged_null) {
        logged_null = 1;
        logged_count++;
        record(lib, symbol, "(null)");
      }
    } else {
      unsigned h = (unsigned)(((uintptr_t)selector >> 3) % SEL_SLOTS);
      while (logged_sels[h] && logged_sels[h] != selector) h = (h + 1) % SEL_SLOTS;
      if (!logged_sels[h]) {
        logged_sels[h] = selector;
        logged_count++;
        record(lib, symbol, selector);
      }
    }
    if (logged_count == SEL_LOG_MAX) {
      char note[64];
      snprintf(note, sizeof note, "selector log full (%d), further selectors not logged", SEL_LOG_MAX);
      record(lib, "", note);
      logged_count++;  // past the limit: stop looking
    }
  }
  pthread_mutex_unlock(&lock);
}
