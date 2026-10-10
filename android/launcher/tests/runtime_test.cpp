// libmcfm_runtime.so (docs/LAUNCHER.md, Stage 3a): the Apple-ABI libc++ on Android. Object
// layouts the game shares with libc++ are Apple's, exceptions unwind, threads work over the
// Darwin pthread layer, ctype reads Darwin's tables, type_info compares like Apple arm64.
// Compiled against the runtime's headers, run on the device (make android-test).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <condition_variable>
#include <ios>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#include "darwin.h"

extern "C" void mcfm_darwin_CCHmacInit(void *, uint32_t, const void *, size_t);
extern "C" void mcfm_darwin_CCHmacFinal(void *, void *);
extern "C" int mcfm_darwin_CCCrypt(int, int, int, const void *, size_t, const void *, const void *, size_t, void *,
                                   size_t, size_t *);

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

__attribute__((noinline)) int deep(int n) {
  std::string keep(40, 'x');  // a destructor on every frame
  if (n == 0) throw std::runtime_error("deep");
  return deep(n - 1) + static_cast<int>(keep.size());
}

struct NamedTypeInfo : std::type_info {
  explicit NamedTypeInfo(const char *name) : std::type_info(name) {}
};

std::mutex g_static_mutex;  // constant-initialized with Darwin's initializer

}  // namespace

int main() {
  // 1. std::string: Apple arm64's alternate layout. Short: characters at 0, size in the last byte.
  std::string s("abc");
  const unsigned char *b = reinterpret_cast<const unsigned char *>(&s);
  EXPECT(sizeof(std::string) == 24);
  EXPECT(memcmp(b, "abc", 3) == 0 && b[23] == 3);
  // Long: data pointer first, size second, the "long" bit in the last byte's top bit.
  std::string l(100, 'y');
  const unsigned char *lb = reinterpret_cast<const unsigned char *>(&l);
  EXPECT(*reinterpret_cast<const char *const *>(lb) == l.data());
  EXPECT(*reinterpret_cast<const size_t *>(lb + 8) == 100);
  EXPECT((lb[23] & 0x80) != 0);

  // 2. An exception crosses 5 frames with destructors, caught here.
  try {
    deep(5);
    EXPECT(false);
  } catch (const std::runtime_error &e) {
    EXPECT(strcmp(e.what(), "deep") == 0);
  }

  // 3. std::mutex is Darwin's pthread_mutex_t, constant-initialized with its signature.
  EXPECT(sizeof(std::mutex) == darwin::kSizeof_pthread_mutex_t);
  EXPECT(sizeof(std::condition_variable) == darwin::kSizeof_pthread_cond_t);
  long sig;
  memcpy(&sig, &g_static_mutex, sizeof sig);
  EXPECT(sig == darwin::k_PTHREAD_MUTEX_SIG_init);
  // ... and works: threads, a condition variable, a recursive mutex.
  std::mutex m;
  std::condition_variable cv;
  int stage = 0;
  std::thread t([&] {
    std::unique_lock<std::mutex> lock(m);
    stage = 1;
    cv.notify_one();
    cv.wait(lock, [&] { return stage == 2; });
    stage = 3;
  });
  {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return stage == 1; });
    stage = 2;
    cv.notify_one();
  }
  t.join();
  EXPECT(stage == 3);
  std::recursive_mutex rm;
  rm.lock();
  EXPECT(rm.try_lock());
  rm.unlock();
  rm.unlock();
  {
    std::lock_guard<std::mutex> lock(g_static_mutex);
  }
  // A timed wait times out (libc++ checks ETIMEDOUT with bionic's number).
  {
    std::unique_lock<std::mutex> lock(m);
    EXPECT(cv.wait_for(lock, std::chrono::milliseconds(20)) == std::cv_status::timeout);
  }

  // 4. ctype<char> uses Darwin's masks and the shared _RuneLocale table.
  const std::ctype<char> &ct = std::use_facet<std::ctype<char> >(std::locale::classic());
  EXPECT(ct.table() == mcfm_darwin_DefaultRuneLocale.__runetype);
  EXPECT(std::ctype_base::space == 0x4000 && std::ctype_base::alpha == 0x100);
  EXPECT(ct.is(std::ctype_base::space, ' ') && ct.is(std::ctype_base::alpha, 'q') && !ct.is(std::ctype_base::digit, 'q'));
  EXPECT(ct.toupper('a') == 'A' && ct.tolower('Q') == 'q');
  std::istringstream in("  42 abc");
  int n = 0;
  std::string word;
  in >> n >> word;
  EXPECT(n == 42 && word == "abc");

  // 5. type_info: Apple arm64's NonUniqueARMRTTIBit comparison. Names with the top bit set are
  // compared by string, others by address.
  static const char name_a[] = "4Test", name_b[] = "4Test";
  const uint64_t non_unique = 1ull << 63;
  NamedTypeInfo ua(reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(name_a) | non_unique));
  NamedTypeInfo ub(reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(name_b) | non_unique));
  NamedTypeInfo pa(name_a), pb(name_b);
  EXPECT(ua == ub);
  EXPECT(!(pa == pb));
  EXPECT(strcmp(ua.name(), "4Test") == 0);
  EXPECT(typeid(int) == typeid(int) && typeid(int) != typeid(long));

  // 6. Darwin's mbstate_t (128 bytes), so streampos has Apple's layout.
  EXPECT(sizeof(mbstate_t) == 128);
  EXPECT(sizeof(std::streampos) == 136);

  // 7. Every libSystem table entry resolves on this device (bionic names included), sorted.
  const char *names[1024];
  size_t count = mcfm_darwin_symbol_names(names, 1024);
  EXPECT(count <= 1024);
  for (size_t i = 0; i < count && i < 1024; i++) {
    if (!mcfm_darwin_symbol(names[i])) { printf("unresolved libSystem entry: %s\n", names[i]); fails++; }
    if (i > 0 && strcmp(names[i - 1], names[i]) >= 0) { printf("table not sorted at %s\n", names[i]); fails++; }
  }

  // 8. CCCrypt (Xbox Live only) reports kCCUnimplemented and moves nothing.
  size_t moved = 99;
  char out[16];
  EXPECT(mcfm_darwin_CCCrypt(0, 0, 0, "k", 1, nullptr, "in", 2, out, sizeof out, &moved) == -4305 && moved == 0);

  // 9. Linux-only errno numbers do not alias Darwin ones (EBADE is 52, Darwin's ENETRESET) and
  // come back unchanged.
  EXPECT(mcfm_darwin_errno(EBADE) != 52 && mcfm_bionic_errno(mcfm_darwin_errno(EBADE)) == EBADE);
  EXPECT(mcfm_darwin_errno(ENOTEMPTY) == darwin::kENOTEMPTY && mcfm_bionic_errno(darwin::kENOTEMPTY) == ENOTEMPTY);

  // 10. CCHmac with an unsupported algorithm (SHA-1: a 20-byte MAC) writes nothing.
  unsigned char hmac[384], mac[20 + 12];
  memset(mac, 0xAA, sizeof mac);
  mcfm_darwin_CCHmacInit(hmac, static_cast<uint32_t>(darwin::kCCHmacAlgSHA1), "k", 1);
  mcfm_darwin_CCHmacFinal(hmac, mac);
  bool untouched = true;
  for (unsigned char b : mac) untouched &= b == 0xAA;
  EXPECT(untouched);

  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("runtime_test: all passed\n");
  return 0;
}
