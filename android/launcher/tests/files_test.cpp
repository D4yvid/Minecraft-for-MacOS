// Darwin file, system and signal shims on Android (docs/LAUNCHER.md, Stage 3a): Darwin-layout
// structs are filled completely and never written past (guard bytes), with Darwin's numbers.
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <set>
#include <string>

#include "darwin.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

template <typename T> struct Guarded {
  T object;
  unsigned char guard[64];
  Guarded() { memset(&object, 0xEE, sizeof object); memset(guard, 0xA5, sizeof guard); }
  bool intact() const { for (unsigned char g : guard) if (g != 0xA5) return false; return true; }
};

volatile int g_signo = 0;
void handler(int signo) { g_signo = signo; }

}  // namespace

int main() {
  const char *dir = "files_test.dir";
  mkdir(dir, 0755);
  std::string file = std::string(dir) + "/a";
  int fd = open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0640);
  EXPECT(write(fd, "0123456789", 10) == 10);
  close(fd);

  // stat / lstat / fstat into Darwin's 144-byte struct stat.
  Guarded<darwin::stat> st;
  EXPECT(mcfm_darwin_stat(file.c_str(), &st.object) == 0);
  EXPECT(st.object.st_size == 10 && (st.object.st_mode & 0170000) == 0100000 && (st.object.st_mode & 0777) == 0640);
  EXPECT(st.object.st_nlink == 1 && st.object.st_flags == 0 && st.object.st_gen == 0);
  EXPECT(st.object.st_mtimespec.tv_sec > 1700000000 && st.object.st_birthtimespec.tv_sec == st.object.st_ctimespec.tv_sec);
  EXPECT(st.intact());
  std::string link = std::string(dir) + "/l";
  symlink("a", link.c_str());
  EXPECT(mcfm_darwin_lstat(link.c_str(), &st.object) == 0 && (st.object.st_mode & 0170000) == 0120000);
  fd = open(file.c_str(), O_RDONLY);
  EXPECT(mcfm_darwin_fstat(fd, &st.object) == 0 && st.object.st_size == 10);
  close(fd);
  EXPECT(st.intact());

  // readdir lists 300 names with Darwin's d_namlen and d_type; readdir_r too.
  for (int i = 0; i < 300; i++) close(open((std::string(dir) + "/n" + std::to_string(i)).c_str(), O_CREAT | O_WRONLY, 0644));
  void *d = mcfm_darwin_opendir(dir);
  EXPECT(d != nullptr);
  std::set<std::string> names;
  bool layout_ok = true;
  while (darwin::dirent *e = mcfm_darwin_readdir(d)) {
    names.insert(e->d_name);
    layout_ok &= e->d_namlen == strlen(e->d_name) && e->d_reclen == sizeof(darwin::dirent);
    if (strcmp(e->d_name, "l") == 0) layout_ok &= e->d_type == darwin::kDT_LNK;
    if (strcmp(e->d_name, "a") == 0) layout_ok &= e->d_type == darwin::kDT_REG;
  }
  EXPECT(mcfm_darwin_closedir(d) == 0);
  EXPECT(names.size() == 304 && names.count("n299") && names.count(".") && layout_ok);
  d = mcfm_darwin_opendir(dir);
  Guarded<darwin::dirent> entry;
  darwin::dirent *result = nullptr;
  int count = 0;
  while (mcfm_darwin_readdir_r(d, &entry.object, &result) == 0 && result) count++;
  mcfm_darwin_closedir(d);
  EXPECT(count == 304 && entry.intact());

  // mmap with Darwin's MAP_ANON.
  void *m = mcfm_darwin_mmap(nullptr, 65536, darwin::kPROT_READ | darwin::kPROT_WRITE,
                             darwin::kMAP_ANON | darwin::kMAP_PRIVATE, -1, 0);
  EXPECT(m != reinterpret_cast<void *>(-1));
  if (m != reinterpret_cast<void *>(-1)) { memset(m, 1, 65536); munmap(m, 65536); }

  // sysconf, uname, sysctlbyname.
  EXPECT(mcfm_darwin_sysconf(darwin::k_SC_NPROCESSORS_ONLN) > 0);
  EXPECT(mcfm_darwin_sysconf(darwin::k_SC_PAGESIZE) == getpagesize());
  Guarded<darwin::utsname> u;
  EXPECT(mcfm_darwin_uname(&u.object) == 0 && strcmp(u.object.sysname, "Darwin") == 0 &&
         strcmp(u.object.machine, "arm64") == 0 && u.intact());
  int cputype = 0;
  size_t len = sizeof cputype;
  EXPECT(mcfm_darwin_sysctlbyname("hw.cputype", &cputype, &len, nullptr, 0) == 0 && cputype == 0x0100000C);
  char small[3];
  len = sizeof small;
  EXPECT(mcfm_darwin_sysctlbyname("hw.machine", small, &len, nullptr, 0) == -1);  // ENOMEM: too small

  // A handler installed with Darwin's SIGUSR1 (30) gets 30, through raise with 30.
  darwin::sigaction_t sa;
  memset(&sa, 0, sizeof sa);
  sa.handler = handler;
  EXPECT(mcfm_darwin_sigaction(darwin::kSIGUSR1, &sa, nullptr) == 0);
  EXPECT(mcfm_darwin_raise(darwin::kSIGUSR1) == 0);
  EXPECT(g_signo == darwin::kSIGUSR1);

  for (int i = 0; i < 300; i++) unlink((std::string(dir) + "/n" + std::to_string(i)).c_str());
  unlink(link.c_str());
  unlink(file.c_str());
  rmdir(dir);
  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("files_test: all passed\n");
  return 0;
}
