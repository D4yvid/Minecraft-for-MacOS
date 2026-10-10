// Darwin conformance fixture (docs/LAUNCHER.md, Stage 3a): Darwin arm64 code, built on the Mac,
// that calls libSystem and libc++ the way the game does and prints what it sees. On the Mac
// (real Darwin) the transcript is the reference; on Android, loaded by mcfm-run with the Darwin
// layer and the Apple-ABI runtime, it must be identical (tools/tests/android_launcher_test.sh).
// No addresses, pids, times or paths in the output. Works in the current directory.
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <locale.h>
#include <xlocale.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
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
  big = strtol("34", nullptr, 10);  // succeeds: errno untouched
  LINE("errno kept across a successful call %d (%ld)", errno, big);
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
  int rm = rmdir("conformance.dir");
  int rm_errno = errno;
  LINE("rmdir non-empty %d errno %d (ENOTEMPTY %d) %s", rm, rm_errno, ENOTEMPTY, strerror(rm_errno));
  std::string longname(300, 'n');
  int long_fd = open(longname.c_str(), O_RDONLY);
  LINE("open long name %d errno %d (ENAMETOOLONG %d)", long_fd, errno, ENAMETOOLONG);
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
  int missing = stat("c2/missing", &st);
  LINE("stat missing %d errno %d", missing, errno);
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
  DIR *nope = opendir("c2/nope");
  LINE("opendir missing %s errno %d", nope ? "opened" : "null", errno);
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
  int unknown = sysctlbyname("mcfm.nothing", machine, &len, nullptr, 0);
  LINE("sysctl unknown %d errno %d", unknown, errno);

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
  int tc = tcgetattr(fd, &t);
  LINE("tcgetattr %d errno %d", tc, errno);
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
  // 16 blocks wait for a 17th on the same global queue: a fixed pool of ncpu threads would hang.
  dispatch_semaphore_t gate = dispatch_semaphore_create(0), finished = dispatch_semaphore_create(0);
  for (int k = 0; k < 16; k++)
    dispatch_async(pool, ^{
      dispatch_semaphore_wait(gate, DISPATCH_TIME_FOREVER);
      dispatch_semaphore_signal(finished);
    });
  dispatch_async(pool, ^{
    for (int k = 0; k < 16; k++) dispatch_semaphore_signal(gate);
  });
  int finished_count = 0;
  for (int k = 0; k < 16; k++)
    finished_count += dispatch_semaphore_wait(finished, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0;
  LINE("global queue overcommit %d", finished_count);

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
  __block bool caught_async = false;
  dispatch_async(serial, ^{
    try {
      throw std::runtime_error("in async block");
    } catch (const std::runtime_error &) {
      caught_async = true;
    }
  });
  dispatch_sync(serial, ^{});
  LINE("exception inside an async block caught %d", caught_async);
  LINE("semaphore timeout %ld", dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC)));
  dispatch_semaphore_signal(done);
  LINE("semaphore available %ld", dispatch_semaphore_wait(done, DISPATCH_TIME_NOW));
  _dyld_register_func_for_add_image(on_image);
  LINE("dyld add-image callback saw an image %d", g_images_seen);
}

// A loopback TCP round trip for one family: listen on port 0, read the port back, connect, echo.
void tcp_echo(int family) {
  const char *name = family == AF_INET ? "v4" : "v6";
  int server = socket(family, SOCK_STREAM, IPPROTO_TCP);
  int one = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_storage ss;
  memset(&ss, 0, sizeof ss);
  socklen_t len;
  if (family == AF_INET) {
    sockaddr_in *a = reinterpret_cast<sockaddr_in *>(&ss);
    a->sin_len = sizeof *a;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    len = sizeof *a;
  } else {
    sockaddr_in6 *a = reinterpret_cast<sockaddr_in6 *>(&ss);
    a->sin6_len = sizeof *a;
    a->sin6_family = AF_INET6;
    a->sin6_addr = in6addr_loopback;
    len = sizeof *a;
  }
  int b = bind(server, reinterpret_cast<sockaddr *>(&ss), len);
  int l = listen(server, 4);
  sockaddr_storage bound;
  socklen_t blen = sizeof bound;
  getsockname(server, reinterpret_cast<sockaddr *>(&bound), &blen);
  LINE("tcp %s bind %d listen %d getsockname family ok %d len %d sa_len %d", name, b, l, bound.ss_family == family, blen,
       bound.ss_len);
  int client = socket(family, SOCK_STREAM, 0);
  int c = connect(client, reinterpret_cast<sockaddr *>(&bound), blen);
  sockaddr_storage peer;
  socklen_t plen = sizeof peer;
  int accepted = accept(server, reinterpret_cast<sockaddr *>(&peer), &plen);
  LINE("tcp %s connect %d accept ok %d peer family ok %d", name, c, accepted >= 0, peer.ss_family == family);
  setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  int nodelay = 0;
  socklen_t olen = sizeof nodelay;
  getsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nodelay, &olen);
  int type = 0;
  olen = sizeof type;
  getsockopt(client, SOL_SOCKET, SO_TYPE, &type, &olen);
  timeval rcv;
  memset(&rcv, 0xEE, sizeof rcv);
  rcv.tv_sec = 3;
  rcv.tv_usec = 500000;  // a whole number of Linux timer ticks (it rounds socket timeouts up to them)
  int set_timeout = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
  timeval back;
  memset(&back, 0, sizeof back);
  olen = sizeof back;
  getsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &back, &olen);
  LINE("tcp %s SO_RCVTIMEO %d back %ld.%06d", family == AF_INET ? "v4" : "v6", set_timeout, static_cast<long>(back.tv_sec),
       static_cast<int>(back.tv_usec));
  LINE("tcp %s nodelay %d type %d", name, nodelay != 0, type);
  send(client, "ping", 4, 0);
  char buf[16] = {0};
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(accepted, &readable);
  timeval tv;
  memset(&tv, 0xEE, sizeof tv);  // Darwin's tv_usec is 32-bit: the padding after it is garbage
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  int ready = select(accepted + 1, &readable, nullptr, nullptr, &tv);
  ssize_t n = recv(accepted, buf, sizeof buf, 0);
  LINE("tcp %s select %d recv %zd %s", name, ready, n, buf);
  send(accepted, "pong", 4, 0);
  pollfd pfd = {client, POLLIN, 0};
  int polled = poll(&pfd, 1, 2000);
  n = recv(client, buf, sizeof buf, 0);
  LINE("tcp %s poll %d revents in %d recv %zd %.4s", name, polled, (pfd.revents & POLLIN) != 0, n, buf);
  close(accepted);
  close(client);
  close(server);
}

void part4_network() {
  LINE("== 4 network");
  tcp_echo(AF_INET);
  tcp_echo(AF_INET6);

  int a = socket(AF_INET, SOCK_DGRAM, 0), b = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_len = sizeof addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(b, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
  socklen_t len = sizeof addr;
  getsockname(b, reinterpret_cast<sockaddr *>(&addr), &len);
  ssize_t sent = sendto(a, "datagram", 8, 0, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
  char buf[32] = {0};
  sockaddr_in from;
  memset(&from, 0xEE, sizeof from);
  socklen_t flen = sizeof from;
  ssize_t got = recvfrom(b, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &flen);
  char text[64];
  inet_ntop(AF_INET, &from.sin_addr, text, sizeof text);
  LINE("udp sendto %zd recvfrom %zd %s from %s len %d sa_len %d family %d", sent, got, buf, text, flen, from.sin_len,
       from.sin_family);
  close(a);
  close(b);

  addrinfo hints, *res = nullptr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV;
  int r = getaddrinfo("localhost", "80", &hints, &res);
  int v4 = 0, v6 = 0, consistent = 1;
  for (addrinfo *ai = res; ai; ai = ai->ai_next) {
    if (ai->ai_family == AF_INET) v4++;
    if (ai->ai_family == AF_INET6) v6++;
    consistent &= ai->ai_addr->sa_family == ai->ai_family && ai->ai_addr->sa_len == ai->ai_addrlen &&
                  ai->ai_socktype == SOCK_STREAM &&
                  ntohs(reinterpret_cast<sockaddr_in *>(ai->ai_addr)->sin_port) == 80;
  }
  LINE("getaddrinfo localhost %d v4 %d consistent %d", r, v4 > 0, consistent);
  if (res) freeaddrinfo(res);
  hints.ai_flags = AI_NUMERICHOST;
  r = getaddrinfo("::1", nullptr, &hints, &res);
  LINE("getaddrinfo ::1 %d family v6 %d len %d", r, res && res->ai_family == AF_INET6, res ? res->ai_addrlen : 0);
  char host[64] = {0}, serv[16] = {0};
  if (res) {
    reinterpret_cast<sockaddr_in6 *>(res->ai_addr)->sin6_port = htons(8080);
    LINE("getnameinfo %d %s %s", getnameinfo(res->ai_addr, res->ai_addrlen, host, sizeof host, serv, sizeof serv,
                                             NI_NUMERICHOST | NI_NUMERICSERV), host, serv);
    freeaddrinfo(res);
  }
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_flags = AI_V4MAPPED | AI_NUMERICHOST;
  r = getaddrinfo("127.0.0.1", "80", &hints, &res);
  LINE("getaddrinfo v4mapped %d family v6 %d %s", r, res && res->ai_family == AF_INET6,
       res ? inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 *>(res->ai_addr)->sin6_addr, host, sizeof host) : "-");
  if (res) freeaddrinfo(res);
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_NUMERICHOST;
  r = getaddrinfo("not a host name!", nullptr, &hints, &res);
  LINE("getaddrinfo bad %d %s", r, gai_strerror(r));
  in6_addr six;
  LINE("inet_pton v6 %d", inet_pton(AF_INET6, "fe80::1:2", &six));
  LINE("inet_ntop v6 %s", inet_ntop(AF_INET6, &six, text, sizeof text));
  hostent *h = gethostbyname("localhost");
  LINE("gethostbyname %d type %d length %d", h != nullptr, h ? h->h_addrtype : -1, h ? h->h_length : -1);

  int pair = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  LINE("SO_NOSIGPIPE %d", setsockopt(pair, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one));
  int server = socket(AF_INET, SOCK_STREAM, 0);
  addr.sin_port = 0;
  bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
  listen(server, 1);
  len = sizeof addr;
  getsockname(server, reinterpret_cast<sockaddr *>(&addr), &len);
  connect(pair, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
  close(accept(server, nullptr, nullptr));
  usleep(50000);
  ssize_t w = 0;
  for (int k = 0; k < 200 && w >= 0; k++) {  // the peer is gone: EPIPE once its reset arrives, no signal
    w = write(pair, "x", 1);
    if (w >= 0) usleep(5000);
  }
  LINE("write to closed peer %zd errno %d (EPIPE %d)", w, errno, EPIPE);
  close(pair);
  close(server);

  ifaddrs *ifs = nullptr;
  int loopback = 0;
  timeval wait;
  memset(&wait, 0xEE, sizeof wait);
  wait.tv_sec = 0;
  wait.tv_usec = 30000;
  int idle = socket(AF_INET, SOCK_DGRAM, 0);
  fd_set none;
  FD_ZERO(&none);
  FD_SET(idle, &none);
  LINE("select timeout %d", select(idle + 1, &none, nullptr, nullptr, &wait));
  close(idle);
  LINE("getifaddrs %d", getifaddrs(&ifs));
  for (ifaddrs *i = ifs; i; i = i->ifa_next)
    if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_LOOPBACK) &&
        reinterpret_cast<sockaddr_in *>(i->ifa_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
        i->ifa_addr->sa_len == sizeof(sockaddr_in) && (i->ifa_flags & IFF_UP) && if_nametoindex(i->ifa_name) > 0)
      loopback = 1;
  freeifaddrs(ifs);
  LINE("getifaddrs loopback 127.0.0.1 %d", loopback);
}

std::string hex(const unsigned char *p, size_t n) {
  std::string s;
  char b[3];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof b, "%02x", p[i]);
    s += b;
  }
  return s;
}

void part5_locale_ctype_crypto() {
  LINE("== 5 locale, ctype, CommonCrypto");
  // Darwin's ctype macros read _DefaultRuneLocale for ASCII and call __maskrune above it.
  std::string classes;
  for (int c = 0; c < 256; c++) {
    int bits = (isalpha(c) ? 1 : 0) | (isdigit(c) ? 2 : 0) | (isspace(c) ? 4 : 0) | (ispunct(c) ? 8 : 0) |
               (isupper(c) ? 16 : 0) | (isprint(c) ? 32 : 0) | (iscntrl(c) ? 64 : 0) | (isxdigit(c) ? 128 : 0);
    char b[4];
    snprintf(b, sizeof b, "%02x", bits);
    classes += b;
  }
  LINE("ctype %s", classes.c_str());
  std::string mapped;
  for (int c = 32; c < 128; c++) {
    mapped += static_cast<char>(toupper(c));
    mapped += static_cast<char>(tolower(c));
  }
  LINE("toupper/tolower %s", mapped.c_str());
  LINE("toupper 0xe9 %d tolower 0xc9 %d", toupper(0xe9), tolower(0xc9));
  LINE("setlocale query %s", setlocale(LC_ALL, nullptr));
  LINE("setlocale C %s", setlocale(LC_ALL, "C") ? "ok" : "null");
  LINE("setlocale numeric %s", setlocale(LC_NUMERIC, nullptr));
  locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
  LINE("newlocale %d", c_locale != nullptr);
  locale_t previous = uselocale(c_locale);
  LINE("uselocale previous global %d", previous == LC_GLOBAL_LOCALE);
  uselocale(previous);
  freelocale(c_locale);
  LINE("newlocale numeric %d", newlocale(LC_NUMERIC_MASK, "C", nullptr) != nullptr);
  locale_t null_name = newlocale(LC_CTYPE_MASK, nullptr, nullptr);  // NULL name: the C locale
  LINE("newlocale null name %d", null_name != nullptr);
  if (null_name) freelocale(null_name);

  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_CTX sha;
  CC_SHA256_Init(&sha);
  CC_SHA256_Update(&sha, "abc", 3);
  CC_SHA256_Final(digest, &sha);
  LINE("sha256 abc %s", hex(digest, sizeof digest).c_str());
  std::string million(1000000, 'a');
  CC_SHA256_Init(&sha);
  for (size_t off = 0; off < million.size(); off += 777)
    CC_SHA256_Update(&sha, million.data() + off, static_cast<CC_LONG>(std::min<size_t>(777, million.size() - off)));
  CC_SHA256_Final(digest, &sha);
  LINE("sha256 million a %s", hex(digest, sizeof digest).c_str());
  CCHmacContext hmac;
  const char *data = "what do ya want for nothing?";
  CCHmacInit(&hmac, kCCHmacAlgSHA256, "Jefe", 4);
  CCHmacUpdate(&hmac, data, strlen(data));
  CCHmacFinal(&hmac, digest);
  LINE("hmac-sha256 rfc4231-2 %s", hex(digest, sizeof digest).c_str());
  std::string long_key(131, '\xaa');
  const char *big = "Test Using Larger Than Block-Size Key - Hash Key First";
  CCHmacInit(&hmac, kCCHmacAlgSHA256, long_key.data(), long_key.size());
  CCHmacUpdate(&hmac, big, strlen(big));
  CCHmacFinal(&hmac, digest);
  LINE("hmac-sha256 rfc4231-6 %s", hex(digest, sizeof digest).c_str());
}

}  // namespace

// part 0: all parts. Returns 0.
extern "C" int conformance_main(int part) {
  if (part == 0 || part == 1) part1_stdio_errno();
  if (part == 0 || part == 2) part2_files_system_signals();
  if (part == 0 || part == 3) part3_mach_blocks_dispatch();
  if (part == 0 || part == 4) part4_network();
  if (part == 0 || part == 5) part5_locale_ctype_crypto();
  fflush(stdout);
  return 0;
}
