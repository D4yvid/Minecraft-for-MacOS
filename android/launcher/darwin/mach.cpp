// Darwin's Mach APIs the game uses, on Linux: time (mach_absolute_time in nanoseconds, timebase
// 1/1), the host's page size and memory statistics, and Mach semaphores (bionic sem_t behind a
// small handle table: a semaphore_t is a 32-bit port name, too small for a pointer). Plus the
// rest of the Darwin-only odds: kqueue (unsupported), hash_* (unsupported) and
// _dyld_register_func_for_add_image (called back with the images our loader registered).
#include <errno.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mutex>

#include "darwin.h"

namespace {

constexpr int kKernSuccess = static_cast<int>(darwin::kKERN_SUCCESS);
constexpr int kKernInvalidArgument = static_cast<int>(darwin::kKERN_INVALID_ARGUMENT);
constexpr int kKernAborted = 14;  // KERN_ABORTED

struct VmStatistics {  // vm_statistics_data_t: natural_t (32-bit) counters
  uint32_t free_count, active_count, inactive_count, wire_count;
  uint32_t rest[11];
};
static_assert(sizeof(VmStatistics) == darwin::kSizeof_vm_statistics, "vm_statistics");
static_assert(offsetof(VmStatistics, wire_count) == darwin::kOffsetof_vm_statistics_wire_count, "wire_count");

// /proc/meminfo value in kB, or 0.
uint64_t meminfo_kb(const char *key) {
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f) return 0;
  char line[256];
  uint64_t value = 0;
  size_t n = strlen(key);
  while (fgets(line, sizeof line, f))
    if (strncmp(line, key, n) == 0 && line[n] == ':') {
      value = strtoull(line + n + 1, nullptr, 10);
      break;
    }
  fclose(f);
  return value;
}

constexpr int kMaxSemaphores = 1024;
sem_t *g_semaphores[kMaxSemaphores];
std::mutex g_semaphores_lock;

sem_t *semaphore(uint32_t name) {
  std::lock_guard<std::mutex> hold(g_semaphores_lock);
  return name >= 1 && name <= kMaxSemaphores ? g_semaphores[name - 1] : nullptr;
}

// Images our loader mapped, for _dyld_register_func_for_add_image.
struct LoadedImage { const void *header; intptr_t slide; };
constexpr int kMaxImages = 8;
LoadedImage g_images[kMaxImages];
int g_image_count = 0;
typedef void (*AddImageCallback)(const void *header, intptr_t slide);
AddImageCallback g_callbacks[16];
int g_callback_count = 0;
std::mutex g_images_lock;

}  // namespace

extern "C" {

uint32_t mcfm_darwin_mach_task_self_ = 0x103;  // any non-null port name

uint64_t mcfm_darwin_mach_absolute_time(void) {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint64_t>(t.tv_sec) * 1000000000ull + static_cast<uint64_t>(t.tv_nsec);
}

int mcfm_darwin_mach_timebase_info(uint32_t *info) {  // {numer, denom}
  info[0] = 1;
  info[1] = 1;
  return kKernSuccess;
}

uint32_t mcfm_darwin_mach_host_self(void) { return 0x203; }

int mcfm_darwin_host_page_size(uint32_t, uintptr_t *size) {
  *size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
  return kKernSuccess;
}

int mcfm_darwin_host_statistics(uint32_t, int flavor, int *info, uint32_t *count) {
  if (flavor != darwin::kHOST_VM_INFO || !count || *count < static_cast<uint32_t>(darwin::kHOST_VM_INFO_COUNT))
    return kKernInvalidArgument;
  VmStatistics s;
  memset(&s, 0, sizeof s);
  uint64_t page_kb = static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) / 1024;
  s.free_count = static_cast<uint32_t>(meminfo_kb("MemFree") / page_kb);
  s.active_count = static_cast<uint32_t>(meminfo_kb("Active") / page_kb);
  s.inactive_count = static_cast<uint32_t>(meminfo_kb("Inactive") / page_kb);
  s.wire_count = static_cast<uint32_t>(meminfo_kb("Unevictable") / page_kb);
  memcpy(info, &s, sizeof s);
  *count = static_cast<uint32_t>(darwin::kHOST_VM_INFO_COUNT);
  return kKernSuccess;
}

int mcfm_darwin_semaphore_create(uint32_t, uint32_t *out, int, int value) {
  sem_t *s = static_cast<sem_t *>(malloc(sizeof(sem_t)));
  if (!s || sem_init(s, 0, static_cast<unsigned>(value)) != 0) {
    free(s);
    return kKernInvalidArgument;
  }
  std::lock_guard<std::mutex> hold(g_semaphores_lock);
  for (int i = 0; i < kMaxSemaphores; i++)
    if (!g_semaphores[i]) {
      g_semaphores[i] = s;
      *out = static_cast<uint32_t>(i + 1);
      return kKernSuccess;
    }
  sem_destroy(s);
  free(s);
  mcfm_darwin_log_once("semaphore_create: more than 1024 Mach semaphores");
  return 6;  // KERN_RESOURCE_SHORTAGE
}

int mcfm_darwin_semaphore_destroy(uint32_t, uint32_t name) {
  std::lock_guard<std::mutex> hold(g_semaphores_lock);
  if (name < 1 || name > kMaxSemaphores || !g_semaphores[name - 1]) return kKernInvalidArgument;
  sem_destroy(g_semaphores[name - 1]);
  free(g_semaphores[name - 1]);
  g_semaphores[name - 1] = nullptr;
  return kKernSuccess;
}

int mcfm_darwin_semaphore_signal(uint32_t name) {
  sem_t *s = semaphore(name);
  return s && sem_post(s) == 0 ? kKernSuccess : kKernInvalidArgument;
}

int mcfm_darwin_semaphore_wait(uint32_t name) {
  sem_t *s = semaphore(name);
  if (!s) return kKernInvalidArgument;
  while (sem_wait(s) != 0)
    if (errno != EINTR) return kKernAborted;
  return kKernSuccess;
}

int mcfm_darwin_kqueue(void) {
  mcfm_darwin_log_once("kqueue: not supported on Android");
  errno = ENOSYS;
  return -1;
}

int mcfm_darwin_kevent(int, const void *, int, void *, int, const void *) {
  errno = EBADF;
  return -1;
}

void *mcfm_darwin_hash_create(int) {
  mcfm_darwin_log_once("hash_create: not supported on Android");
  return nullptr;
}

void *mcfm_darwin_hash_search(void *, const void *, int, void *, void *) { return nullptr; }

void mcfm_darwin_add_image(const void *header, intptr_t slide) {
  AddImageCallback callbacks[16];
  int n;
  {
    std::lock_guard<std::mutex> hold(g_images_lock);
    if (g_image_count < kMaxImages) g_images[g_image_count++] = LoadedImage{header, slide};
    n = g_callback_count;
    memcpy(callbacks, g_callbacks, sizeof callbacks);
  }
  for (int i = 0; i < n; i++) callbacks[i](header, slide);
}

// Darwin calls a new callback at once for every loaded image, then for each new one.
void mcfm_darwin__dyld_register_func_for_add_image(AddImageCallback callback) {
  LoadedImage images[kMaxImages];
  int n;
  {
    std::lock_guard<std::mutex> hold(g_images_lock);
    if (g_callback_count < 16) g_callbacks[g_callback_count++] = callback;
    n = g_image_count;
    memcpy(images, g_images, sizeof images);
  }
  for (int i = 0; i < n; i++) callback(images[i].header, images[i].slide);
}

}  // extern "C"
