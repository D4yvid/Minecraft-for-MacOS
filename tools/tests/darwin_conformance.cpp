// Darwin conformance fixture (docs/LAUNCHER.md, Stage 3a): Darwin arm64 code, built on the Mac,
// that calls libSystem and libc++ the way the game does and prints what it sees. On the Mac
// (real Darwin) the transcript is the reference; on Android, loaded by mcfm-run with the Darwin
// layer and the Apple-ABI runtime, it must be identical (tools/tests/android_launcher_test.sh).
// No addresses, pids, times or paths in the output. Works in the current directory.
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
// The game calls these deprecated APIs too.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <libkern/OSAtomic.h>

#include <stdexcept>
#include <string>

namespace {

#define LINE(...) printf(__VA_ARGS__), printf("\n")

// A Darwin va_list (char *) handed to vsnprintf.
int format_va(char *out, size_t n, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int r = vsnprintf(out, n, format, ap);
  va_end(ap);
  return r;
}

int compare_throwing(const void *a, const void *b) {
  int x = *static_cast<const int *>(a), y = *static_cast<const int *>(b);
  if (x == 13 || y == 13) throw std::runtime_error("unlucky");
  return x - y;
}

int compare_ints(const void *a, const void *b) {
  return *static_cast<const int *>(a) - *static_cast<const int *>(b);
}

void part1_stdio_errno() {
  LINE("== 1 stdio, variadics, errno");
  char buf[256];
  int n = snprintf(buf, sizeof buf, "%d|%ld|%lld|%u|%x|%o|%c|%s|%5.2f|%e|%g|%%|%*d|%-5s|%zu|%hhd", -7, 1234567890123L,
                   -9000000000000LL, 42u, 255u, 8u, 'Z', "str", 3.14159, 12345.678, 0.0001, 6, 42, "ab", sizeof(int),
                   static_cast<signed char>(-3));
  LINE("snprintf %d [%s]", n, buf);
  n = format_va(buf, sizeof buf, "%s-%d-%.3f-%c", "va", 99, 2.5, 'q');
  LINE("vsnprintf %d [%s]", n, buf);
  n = sprintf(buf, "%08.3f|%+d|%#x", -1.5, 5, 0xbeef);
  LINE("sprintf %d [%s]", n, buf);
  char *heap = nullptr;
  n = asprintf(&heap, "%s and %d doubles %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f", "many", 9, 1.0, 2.0, 3.0, 4.0,
               5.0, 6.0, 7.0, 8.0, 9.0);
  LINE("asprintf %d [%s]", n, heap);
  free(heap);
  int i = 0;
  float f = 0;
  char word[32] = {0};
  n = sscanf("12 3.5 word", "%d %f %31s", &i, &f, word);
  LINE("sscanf %d %d %.1f %s", n, i, f, word);

  FILE *out = fopen("conformance.tmp", "w");
  fprintf(out, "%d %s %.2f\n", 77, "filed", 0.25);
  fclose(out);
  FILE *in = fopen("conformance.tmp", "r");
  n = fscanf(in, "%d %31s %f", &i, word, &f);
  LINE("fscanf %d %d %s %.2f", n, i, word, f);
  fclose(in);

  errno = 0;
  long big = strtol("99999999999999999999", nullptr, 10);
  LINE("strtol overflow %ld errno %d", big, errno);
  errno = 0;
  big = strtol("12", nullptr, 10);
  LINE("strtol ok %ld errno %d", big, errno);
  errno = 123;
  LINE("errno kept %d", errno);
  int fd = open("conformance.tmp", O_CREAT | O_EXCL | O_WRONLY, 0644);
  LINE("open O_EXCL existing %d errno %d %s", fd, errno, strerror(errno));
  unlink("conformance.tmp");
  fd = open("conformance.tmp", O_CREAT | O_EXCL | O_WRONLY, 0600);
  LINE("open O_EXCL new %s", fd >= 0 ? "ok" : "failed");
  int flags = fcntl(fd, F_GETFL);
  LINE("fcntl F_GETFL accmode %d", flags & O_ACCMODE);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  LINE("fcntl O_NONBLOCK set %d", (fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);
  close(fd);
  unlink("conformance.tmp");
  mkdir("conformance.dir", 0755);
  close(open("conformance.dir/file", O_CREAT | O_WRONLY, 0644));
  LINE("rmdir non-empty %d errno %d (ENOTEMPTY %d) %s", rmdir("conformance.dir"), errno, ENOTEMPTY, strerror(errno));
  std::string longname(300, 'n');
  LINE("open long name %d errno %d (ENAMETOOLONG %d)", open(longname.c_str(), O_RDONLY), errno, ENAMETOOLONG);
  LINE("strerror_r %d [%s]", strerror_r(EAGAIN, buf, sizeof buf), buf);
  unlink("conformance.dir/file");
  rmdir("conformance.dir");

  unsigned char pattern[16], target[40];
  for (int k = 0; k < 16; k++) pattern[k] = static_cast<unsigned char>('a' + k);
  memset_pattern16(target, pattern, sizeof target);
  LINE("memset_pattern16 %.40s", reinterpret_cast<char *>(target));
  volatile double angle = 0.75;
  double s = sin(angle), c = cos(angle);  // clang emits __sincos_stret on Darwin
  LINE("sincos %.6f %.6f", s, c);
  volatile float anglef = 0.5f;
  float sf = sinf(anglef), cf = cosf(anglef);
  LINE("sincosf %.5f %.5f", sf, cf);
  OSMemoryBarrier();
  LINE("atoi %d atof %.2f strtoul %lu", atoi("-321"), atof("6.25"), strtoul("ff", nullptr, 16));

  int values[] = {5, 3, 13, 1, 4};
  try {
    qsort(values, 5, sizeof(int), compare_throwing);
    LINE("qsort did not throw");
  } catch (const std::runtime_error &e) {
    LINE("qsort comparator threw: %s", e.what());
  }
  qsort(values, 5, sizeof(int), compare_ints);
  LINE("qsort %d %d %d %d %d", values[0], values[1], values[2], values[3], values[4]);
}

}  // namespace

// part 0: all parts. Returns 0.
extern "C" int conformance_main(int part) {
  if (part == 0 || part == 1) part1_stdio_errno();
  fflush(stdout);
  return 0;
}
