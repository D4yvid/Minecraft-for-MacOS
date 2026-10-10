// Darwin-only libSystem functions and data the game uses, and checks that the structs it shares
// with bionic's pass-through functions have the same layout.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "darwin.h"

static_assert(darwin::kSizeof_tm == sizeof(struct tm) && darwin::kOffsetof_tm_tm_gmtoff == offsetof(struct tm, tm_gmtoff) &&
                  darwin::kOffsetof_tm_tm_zone == offsetof(struct tm, tm_zone),
              "struct tm (gmtime, localtime, mktime, strftime pass through)");
static_assert(darwin::kSizeof_timeval == sizeof(struct timeval) &&
                  darwin::kOffsetof_timeval_tv_usec == offsetof(struct timeval, tv_usec),
              "struct timeval (gettimeofday passes through)");
static_assert(darwin::kSizeof_timespec == sizeof(struct timespec), "struct timespec");
static_assert(darwin::kCLOCKS_PER_SEC == CLOCKS_PER_SEC, "clock()");
static_assert(darwin::kF_OK == F_OK && darwin::kR_OK == R_OK && darwin::kW_OK == W_OK && darwin::kX_OK == X_OK, "access()");

extern "C" {

// Darwin's stdio globals hold bionic's FILE pointers (FILE stays opaque to the game).
FILE *mcfm_darwin_stdinp = stdin;
FILE *mcfm_darwin_stdoutp = stdout;
FILE *mcfm_darwin_stderrp = stderr;

// The image's stack protector compares against this word; any value fixed before the image runs.
uintptr_t mcfm_darwin_stack_chk_guard = 0x6D63666D2D677561ul ^ static_cast<uintptr_t>(arc4random());

void mcfm_darwin_memset_pattern16(void *b, const void *pattern16, size_t len) {
  unsigned char *p = static_cast<unsigned char *>(b);
  while (len >= 16) {
    memcpy(p, pattern16, 16);
    p += 16;
    len -= 16;
  }
  memcpy(p, pattern16, len);
}

// Darwin returns both results in d0/d1 (s0/s1): an AAPCS64 homogeneous aggregate does the same.
struct mcfm_darwin_sincos { double sinval, cosval; };
struct mcfm_darwin_sincosf { float sinval, cosval; };
mcfm_darwin_sincos mcfm_darwin___sincos_stret(double x) { return mcfm_darwin_sincos{sin(x), cos(x)}; }
mcfm_darwin_sincosf mcfm_darwin___sincosf_stret(float x) { return mcfm_darwin_sincosf{sinf(x), cosf(x)}; }

int mcfm_darwin___signbitf(float x) { return signbit(x) ? 1 : 0; }

void mcfm_darwin___assert_rtn(const char *function, const char *file, int line, const char *expression) {
  fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n", expression, function, file, line);
  abort();
}

void mcfm_darwin_OSMemoryBarrier(void) { __sync_synchronize(); }

// 64-bit bionic has no bzero (POSIX removed it).
void mcfm_darwin_bzero(void *p, size_t n) { memset(p, 0, n); }

// Newer Darwin SDKs' FD_SET/FD_ISSET check the descriptor through this (not the 2016 game's).
int mcfm_darwin___darwin_check_fd_set_overflow(int fd, const void *, int unlimited) {
  return unlimited || (fd >= 0 && fd < FD_SETSIZE);
}

}  // extern "C"
