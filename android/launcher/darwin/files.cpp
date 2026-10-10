// Darwin file APIs on bionic: open flags, fcntl commands and ioctl requests (numbers differ).
// Failures leave bionic's errno set; ___error translates it when the game reads it.
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "darwin.h"

namespace {

struct FlagPair { long darwin; int bionic; };
const FlagPair kOpenFlags[] = {
    {darwin::kO_NONBLOCK, O_NONBLOCK}, {darwin::kO_APPEND, O_APPEND},   {darwin::kO_CREAT, O_CREAT},
    {darwin::kO_TRUNC, O_TRUNC},       {darwin::kO_EXCL, O_EXCL},       {darwin::kO_NOFOLLOW, O_NOFOLLOW},
    {darwin::kO_CLOEXEC, O_CLOEXEC},   {darwin::kO_DIRECTORY, O_DIRECTORY}, {darwin::kO_SYNC, O_SYNC},
    {darwin::kO_NOCTTY, O_NOCTTY},     {darwin::kO_DSYNC, O_DSYNC},
};
static_assert(darwin::kO_RDONLY == O_RDONLY && darwin::kO_WRONLY == O_WRONLY && darwin::kO_RDWR == O_RDWR &&
                  darwin::kO_ACCMODE == O_ACCMODE,
              "access modes");
static_assert(darwin::kFD_CLOEXEC == FD_CLOEXEC, "FD_CLOEXEC");

int to_bionic_open_flags(int d) {
  int b = d & O_ACCMODE;
  for (const FlagPair &p : kOpenFlags)
    if (d & p.darwin) b |= p.bionic;
  return b;
}

int to_darwin_open_flags(int b) {
  int d = b & O_ACCMODE;
  for (const FlagPair &p : kOpenFlags)
    if ((b & p.bionic) == p.bionic) d |= static_cast<int>(p.darwin);
  // bionic's O_SYNC contains the O_DSYNC bit; Darwin keeps them apart.
  if ((b & O_SYNC) == O_SYNC) d &= ~static_cast<int>(darwin::kO_DSYNC);
  return d;
}

// struct flock: Darwin {l_start, l_len, l_pid, l_type, l_whence}, bionic {l_type, l_whence, ...}.
struct DarwinFlock { int64_t l_start, l_len; int32_t l_pid; int16_t l_type, l_whence; };
static_assert(sizeof(DarwinFlock) == darwin::kSizeof_flock, "flock");
static_assert(offsetof(DarwinFlock, l_type) == darwin::kOffsetof_flock_l_type, "flock.l_type");
static_assert(darwin::kSEEK_SET == SEEK_SET && darwin::kSEEK_CUR == SEEK_CUR && darwin::kSEEK_END == SEEK_END, "SEEK_*");

short lock_type_to_bionic(short d) {
  return d == darwin::kF_RDLCK ? F_RDLCK : d == darwin::kF_WRLCK ? F_WRLCK : d == darwin::kF_UNLCK ? F_UNLCK : -1;
}
short lock_type_to_darwin(short b) {
  return static_cast<short>(b == F_RDLCK ? darwin::kF_RDLCK : b == F_WRLCK ? darwin::kF_WRLCK : darwin::kF_UNLCK);
}

int lock(int fd, int cmd, DarwinFlock *d) {
  struct flock b;
  memset(&b, 0, sizeof b);
  b.l_type = lock_type_to_bionic(d->l_type);
  b.l_whence = d->l_whence;
  b.l_start = d->l_start;
  b.l_len = d->l_len;
  b.l_pid = d->l_pid;
  int r = fcntl(fd, cmd, &b);
  if (r == 0 && cmd == F_GETLK) {
    d->l_type = lock_type_to_darwin(b.l_type);
    d->l_whence = b.l_whence;
    d->l_start = b.l_start;
    d->l_len = b.l_len;
    d->l_pid = b.l_pid;
  }
  return r;
}

}  // namespace

extern "C" {

int mcfm_darwin_open_needs_mode(int darwin_flags) { return (darwin_flags & darwin::kO_CREAT) != 0; }

int mcfm_darwin_open_impl(const char *path, int darwin_flags, int mode) {
  return open(path, to_bionic_open_flags(darwin_flags), mode);
}

int mcfm_darwin_fcntl_impl(int fd, int cmd, intptr_t arg) {
  if (cmd == darwin::kF_GETFL) {
    int r = fcntl(fd, F_GETFL);
    return r < 0 ? r : to_darwin_open_flags(r);
  }
  if (cmd == darwin::kF_SETFL) return fcntl(fd, F_SETFL, to_bionic_open_flags(static_cast<int>(arg)));
  if (cmd == darwin::kF_GETFD) return fcntl(fd, F_GETFD);
  if (cmd == darwin::kF_SETFD) return fcntl(fd, F_SETFD, static_cast<int>(arg));
  if (cmd == darwin::kF_DUPFD) return fcntl(fd, F_DUPFD, static_cast<int>(arg));
  if (cmd == darwin::kF_DUPFD_CLOEXEC) return fcntl(fd, F_DUPFD_CLOEXEC, static_cast<int>(arg));
  if (cmd == darwin::kF_GETLK) return lock(fd, F_GETLK, reinterpret_cast<DarwinFlock *>(arg));
  if (cmd == darwin::kF_SETLK) return lock(fd, F_SETLK, reinterpret_cast<DarwinFlock *>(arg));
  if (cmd == darwin::kF_SETLKW) return lock(fd, F_SETLKW, reinterpret_cast<DarwinFlock *>(arg));
  if (cmd == darwin::kF_NOCACHE) return 0;  // a caching hint
  if (cmd == darwin::kF_FULLFSYNC) return fsync(fd);
  mcfm_darwin_log_once("fcntl: unsupported Darwin command");
  errno = EINVAL;
  return -1;
}

int mcfm_darwin_ioctl_impl(int fd, unsigned long request, intptr_t arg) {
  if (request == static_cast<unsigned long>(darwin::kFIONBIO)) return ioctl(fd, FIONBIO, reinterpret_cast<int *>(arg));
  if (request == static_cast<unsigned long>(darwin::kFIONREAD)) return ioctl(fd, FIONREAD, reinterpret_cast<int *>(arg));
  int result;
  if (mcfm_darwin_interface_ioctl(fd, request, reinterpret_cast<void *>(arg), &result)) return result;
  mcfm_darwin_log_once("ioctl: unsupported Darwin request");
  errno = ENOTTY;
  return -1;
}

}  // extern "C"
