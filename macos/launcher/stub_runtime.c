#include "stub_runtime.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Called with `lock` held.
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
  pthread_mutex_lock(&lock);
  if (!*seen) {
    *seen = 1;
    record(lib, symbol, 0);
  }
  pthread_mutex_unlock(&lock);
}

// Selectors already logged, keyed by pointer (each selector name is one string in the image).
#define SEL_SLOTS 8192
static const char *logged_sels[SEL_SLOTS];

void mcfm_stub_msgsend(const char *lib, const char *symbol, const char *selector) {
  pthread_mutex_lock(&lock);
  unsigned h = (unsigned)(((uintptr_t)selector >> 3) % SEL_SLOTS);
  for (unsigned i = 0; i < SEL_SLOTS; i++, h = (h + 1) % SEL_SLOTS) {
    if (logged_sels[h] == selector) break;
    if (!logged_sels[h]) {
      logged_sels[h] = selector;
      record(lib, symbol, selector ? selector : "(null)");
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}
