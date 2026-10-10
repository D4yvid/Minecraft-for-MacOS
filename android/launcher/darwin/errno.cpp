// errno numbers: bionic (Linux) <-> Darwin. The names both define come from the generated
// MCFM_DARWIN_ERRNOS list; values bionic shares between two names (EAGAIN/EWOULDBLOCK,
// EOPNOTSUPP/ENOTSUP, EDEADLK/EDEADLOCK) take the listed name.
#include <errno.h>
#include <stdio.h>
#include <string.h>

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

// The game reads and writes errno through ___error(). Darwin's value lives in a thread-local;
// bionic's errno, set by every failing bionic call (and by our shims, in bionic numbers), is
// moved into it, translated, whenever the game asks. bionic's is then cleared, so a later
// failure is seen as new even if it has the same number, and a value the game wrote (errno = 0
// before strtol) stays until something fails again.
namespace {
__thread int t_darwin_errno;
}

extern "C" int *mcfm_darwin___error(void) {
  if (errno != 0) {
    t_darwin_errno = mcfm_darwin_errno(errno);
    errno = 0;
  }
  return &t_darwin_errno;
}

#include "darwin_strerror.inc"

// Darwin's texts (the game may show or compare them); numbers Darwin does not know get bionic's.
extern "C" char *mcfm_darwin_strerror(int darwin_errno) {
  if (darwin_errno >= 0 && darwin_errno < static_cast<int>(sizeof kDarwin_strerror / sizeof kDarwin_strerror[0]))
    return const_cast<char *>(kDarwin_strerror[darwin_errno]);
  return strerror(mcfm_bionic_errno(darwin_errno));
}

// Darwin's strerror_r is POSIX's: 0, or ERANGE when the message does not fit (truncated).
extern "C" int mcfm_darwin_strerror_r(int darwin_errno, char *buf, size_t n) {
  if (n == 0) return darwin::kERANGE;
  const char *message = mcfm_darwin_strerror(darwin_errno);
  size_t length = strlen(message);
  size_t copied = length < n - 1 ? length : n - 1;
  memcpy(buf, message, copied);
  buf[copied] = 0;
  return copied == length ? 0 : static_cast<int>(darwin::kERANGE);
}

extern "C" void mcfm_darwin_perror(const char *s) {
  const char *message = mcfm_darwin_strerror(*mcfm_darwin___error());
  if (s && *s) fprintf(stderr, "%s: %s\n", s, message);
  else fprintf(stderr, "%s\n", message);
}

// getaddrinfo's error texts, by Darwin EAI_* code (the game gets Darwin's codes).
extern "C" const char *mcfm_darwin_gai_strerror(int code) {
  if (code >= 0 && code < static_cast<int>(sizeof kDarwin_gai_strerror / sizeof kDarwin_gai_strerror[0]))
    return kDarwin_gai_strerror[code];
  return "Unknown error";
}
