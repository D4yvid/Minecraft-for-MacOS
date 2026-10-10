// Darwin conformance fixture (docs/LAUNCHER.md, Stage 3a): Darwin arm64 code, built on the Mac,
// that calls libSystem and libc++ the way the game does and prints what it sees. On the Mac
// (real Darwin) the transcript is the reference; on Android, loaded by mcfm-run with the Darwin
// layer and the Apple-ABI runtime, it must be identical (tools/tests/android_launcher_test.sh).
// No addresses, pids, times or paths in the output. Works in the current directory.
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
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
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <pthread.h>

#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>

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

volatile int g_signal = 0;
volatile int g_info_signo = 0;
void on_signal(int signo) { g_signal = signo; }
void on_signal_info(int signo, siginfo_t *info, void *) { g_signal = signo; g_info_signo = info ? info->si_signo : -1; }

void part2_files_system_signals() {
  LINE("== 2 files, mmap, system, signals, time");
  umask(022);  // adb shell's may differ from the Mac's
  mkdir("c2", 0755);
  FILE *f = fopen("c2/file.txt", "w");
  fputs("twelve bytes", f);
  fclose(f);
  symlink("file.txt", "c2/link");
  mkdir("c2/sub", 0700);
  struct stat st;
  memset(&st, 0xAB, sizeof st);
  LINE("stat file %d size %lld reg %d mode %o nlink %d blocks>0 %d blksize>0 %d", stat("c2/file.txt", &st),
       static_cast<long long>(st.st_size), S_ISREG(st.st_mode), st.st_mode & 0777, st.st_nlink, st.st_blocks > 0,
       st.st_blksize > 0);
  LINE("stat mtime recent %d", st.st_mtimespec.tv_sec > 1700000000);
  LINE("lstat link %d lnk %d size %lld", lstat("c2/link", &st), S_ISLNK(st.st_mode), static_cast<long long>(st.st_size));
  LINE("stat via link %d reg %d", stat("c2/link", &st), S_ISREG(st.st_mode));
  LINE("stat dir %d dir %d mode %o", stat("c2/sub", &st), S_ISDIR(st.st_mode), st.st_mode & 0777);
  LINE("stat missing %d errno %d", stat("c2/missing", &st), errno);
  int fd = open("c2/file.txt", O_RDONLY);
  LINE("fstat %d size %lld", fstat(fd, &st), static_cast<long long>(st.st_size));
  void *map = mmap(nullptr, 12, PROT_READ, MAP_PRIVATE, fd, 0);
  LINE("mmap file %.6s", map == MAP_FAILED ? "failed" : static_cast<const char *>(map));
  munmap(map, 12);
  close(fd);
  char *anon = static_cast<char *>(mmap(nullptr, 1 << 16, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  if (anon != MAP_FAILED) strcpy(anon + 40000, "anonymous");
  LINE("mmap anon %s", anon == MAP_FAILED ? "failed" : anon + 40000);
  munmap(anon, 1 << 16);

  for (int k = 0; k < 40; k++) {
    char name[32];
    snprintf(name, sizeof name, "c2/sub/f%02d", k);
    close(open(name, O_CREAT | O_WRONLY, 0644));
  }
  DIR *dir = opendir("c2");
  std::vector<std::string> entries;
  while (struct dirent *e = readdir(dir)) {
    char line[300];
    snprintf(line, sizeof line, "%s:%d:%d", e->d_name, e->d_type, e->d_namlen);
    entries.push_back(line);
  }
  closedir(dir);
  std::sort(entries.begin(), entries.end());
  std::string all;
  for (const std::string &e : entries) all += e + " ";
  LINE("readdir c2 %s", all.c_str());
  dir = opendir("c2/sub");
  int count = 0;
  struct dirent entry, *result = nullptr;
  while (readdir_r(dir, &entry, &result) == 0 && result) count += entry.d_name[0] == 'f' && entry.d_namlen == 3;
  closedir(dir);
  LINE("readdir_r c2/sub files %d", count);
  LINE("opendir missing %s errno %d", opendir("c2/nope") ? "opened" : "null", errno);
  for (int k = 0; k < 40; k++) {
    char name[32];
    snprintf(name, sizeof name, "c2/sub/f%02d", k);
    unlink(name);
  }
  rmdir("c2/sub");
  unlink("c2/link");
  unlink("c2/file.txt");
  rmdir("c2");

  LINE("sysconf cpus %d page %d", sysconf(_SC_NPROCESSORS_ONLN) > 0, sysconf(_SC_PAGESIZE) == getpagesize());
  struct utsname u;
  LINE("uname %d sysname %s machine %s", uname(&u), u.sysname, u.machine);
  char machine[64] = {0};
  size_t len = sizeof machine;
  int cputype = 0;
  size_t ilen = sizeof cputype;
  LINE("sysctl hw.machine %d %s", sysctlbyname("hw.machine", machine, &len, nullptr, 0), machine);
  LINE("sysctl hw.cputype %d %d", sysctlbyname("hw.cputype", &cputype, &ilen, nullptr, 0), cputype);
  LINE("sysctl hw.cpusubtype %d", sysctlbyname("hw.cpusubtype", &cputype, &ilen, nullptr, 0));
  len = sizeof machine;
  LINE("sysctl unknown %d errno %d", sysctlbyname("mcfm.nothing", machine, &len, nullptr, 0), errno);

  struct sigaction sa, old;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  LINE("sigaction SIGUSR1 %d", sigaction(SIGUSR1, &sa, nullptr));
  raise(SIGUSR1);
  LINE("raise SIGUSR1 handled %d (SIGUSR1 %d)", g_signal, SIGUSR1);
  LINE("sigaction query %d same %d", sigaction(SIGUSR1, nullptr, &old), old.sa_handler == on_signal);
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_signal_info;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGUSR2, &sa, nullptr);
  raise(SIGUSR2);
  LINE("siginfo SIGUSR2 %d %d (SIGUSR2 %d)", g_signal, g_info_signo, SIGUSR2);
  void (*previous)(int) = signal(SIGPIPE, SIG_IGN);
  LINE("signal SIGPIPE previous default %d", previous == SIG_DFL);
  LINE("signal SIGPIPE now ignored %d", signal(SIGPIPE, SIG_DFL) == SIG_IGN);
  struct termios t;
  fd = open("c2.tty", O_CREAT | O_RDWR, 0644);  // a regular file: not a terminal on either system
  LINE("tcgetattr %d errno %d", tcgetattr(fd, &t), errno);
  close(fd);
  unlink("c2.tty");

  time_t when = 1234567890;
  struct tm tmv;
  gmtime_r(&when, &tmv);
  char text[64];
  strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S %a %j", &tmv);
  LINE("gmtime_r %s yday %d", text, tmv.tm_yday);
  LINE("timegm %ld", static_cast<long>(timegm(&tmv)));
  struct timeval tv;
  LINE("gettimeofday %d recent %d", gettimeofday(&tv, nullptr), tv.tv_sec > 1700000000 && tv.tv_usec < 1000000);
}

int g_images_seen = 0;
void on_image(const struct mach_header *header, intptr_t) {
  if (header && header->magic == MH_MAGIC_64) g_images_seen = 1;
}

void work_function(void *context) { ++*static_cast<int *>(context); }

semaphore_t g_ping, g_pong;
void *ponger(void *) {
  for (int k = 0; k < 100; k++) {
    semaphore_wait(g_ping);
    semaphore_signal(g_pong);
  }
  return nullptr;
}

void part3_mach_blocks_dispatch() {
  LINE("== 3 mach, blocks, dispatch");
  mach_timebase_info_data_t timebase;
  LINE("mach_timebase_info %d nonzero %d", mach_timebase_info(&timebase), timebase.numer > 0 && timebase.denom > 0);
  uint64_t t0 = mach_absolute_time();
  usleep(20000);
  uint64_t elapsed_ns = (mach_absolute_time() - t0) * timebase.numer / timebase.denom;
  LINE("mach_absolute_time 20ms %d", elapsed_ns >= 15000000 && elapsed_ns < 2000000000);
  vm_size_t page = 0;
  LINE("host_page_size %d matches %d", host_page_size(mach_host_self(), &page), page == static_cast<vm_size_t>(getpagesize()));
  vm_statistics_data_t vm;
  mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
  LINE("host_statistics %d free>0 %d", host_statistics(mach_host_self(), HOST_VM_INFO, reinterpret_cast<host_info_t>(&vm), &count),
       vm.free_count > 0);
  LINE("semaphore_create %d %d", semaphore_create(mach_task_self(), &g_ping, SYNC_POLICY_FIFO, 0),
       semaphore_create(mach_task_self(), &g_pong, SYNC_POLICY_FIFO, 0));
  pthread_t thread;
  pthread_create(&thread, nullptr, ponger, nullptr);
  int rounds = 0;
  for (int k = 0; k < 100; k++) {
    semaphore_signal(g_ping);
    rounds += semaphore_wait(g_pong) == KERN_SUCCESS;
  }
  pthread_join(thread, nullptr);
  LINE("semaphore ping-pong %d", rounds);
  semaphore_destroy(mach_task_self(), g_ping);
  semaphore_destroy(mach_task_self(), g_pong);

  static dispatch_once_t once;
  __block int once_runs = 0;
  dispatch_queue_t pool = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  for (int k = 0; k < 8; k++)
    dispatch_async(pool, ^{
      dispatch_once(&once, ^{
        usleep(10000);
        once_runs++;
      });
      dispatch_semaphore_signal(done);
    });
  for (int k = 0; k < 8; k++) dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  // (The predicate's "done" value is ~0l for the 2016 game's inline check; current libdispatch
  // stores another value, so it is not printed.)
  LINE("dispatch_once runs %d", once_runs);

  dispatch_queue_t serial = dispatch_queue_create("mcfm.conformance", nullptr);
  __block int counter = 0;
  __block bool ordered = true;
  for (int k = 0; k < 1000; k++)
    dispatch_async(serial, ^{
      if (counter != k) ordered = false;
      counter++;
    });
  __block int seen = -1;
  dispatch_sync(serial, ^{ seen = counter; });
  LINE("serial queue %d ordered %d", seen, ordered);
  int plain = 0;
  dispatch_async_f(serial, &plain, work_function);
  dispatch_sync(serial, ^{});
  LINE("dispatch_async_f %d", plain);
  std::string captured = "captured string";
  __block std::string copied;
  dispatch_sync(serial, ^{ copied = captured + " copied"; });
  LINE("block copy %s", copied.c_str());
  __block bool caught = false;
  dispatch_sync(serial, ^{
    try {
      throw std::runtime_error("in block");
    } catch (const std::runtime_error &) {
      caught = true;
    }
  });
  LINE("exception inside a block caught %d", caught);
  LINE("semaphore timeout %ld", dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC)));
  dispatch_semaphore_signal(done);
  LINE("semaphore available %ld", dispatch_semaphore_wait(done, DISPATCH_TIME_NOW));
  _dyld_register_func_for_add_image(on_image);
  LINE("dyld add-image callback saw an image %d", g_images_seen);
}

}  // namespace

// part 0: all parts. Returns 0.
extern "C" int conformance_main(int part) {
  if (part == 0 || part == 1) part1_stdio_errno();
  if (part == 0 || part == 2) part2_files_system_signals();
  if (part == 0 || part == 3) part3_mach_blocks_dispatch();
  fflush(stdout);
  return 0;
}
