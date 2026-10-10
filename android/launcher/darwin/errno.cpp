// errno numbers: bionic (Linux) <-> Darwin. The names both define come from the generated
// MCFM_DARWIN_ERRNOS list; values bionic shares between two names (EAGAIN/EWOULDBLOCK,
// EOPNOTSUPP/ENOTSUP, EDEADLK/EDEADLOCK) take the listed name.
#include <errno.h>
#include <stdio.h>

#include <atomic>

#include "darwin.h"

extern "C" int mcfm_darwin_errno(int e) {
  switch (e) {
    case 0: return 0;
#define MCFM_CASE(name, value) case name: return value;
    MCFM_DARWIN_ERRNOS(MCFM_CASE)
#undef MCFM_CASE
    default: return e;
  }
}

extern "C" int mcfm_bionic_errno(int e) {
  switch (e) {
    case 0: return 0;
#define MCFM_CASE(name, value) case value: return name;
    MCFM_DARWIN_ERRNOS(MCFM_CASE)
#undef MCFM_CASE
    case darwin::kENOTSUP: return ENOTSUP;  // Darwin keeps ENOTSUP (45) apart from EOPNOTSUPP (102)
    default: return e;
  }
}

extern "C" void mcfm_darwin_log_once(const char *message) {
  // A small fixed set of distinct messages (each shim logs a constant string): keep them by
  // pointer, which is stable for string literals.
  static std::atomic<const char *> seen[256];
  for (auto &s : seen) {
    const char *cur = s.load();
    if (cur == message) return;
    if (!cur) {
      if (s.compare_exchange_strong(cur, message)) break;
      if (cur == message) return;
    }
  }
  fprintf(stderr, "mcfm: %s\n", message);
}
