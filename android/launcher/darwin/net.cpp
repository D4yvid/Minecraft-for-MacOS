// Darwin sockets on bionic. Darwin's sockaddr starts with a length byte and a one-byte family
// (AF_INET6 is 30, not 10); bionic's with a two-byte family; after those two bytes AF_INET and
// AF_INET6 addresses are laid out the same, sockaddr_un has a shorter path. Socket options,
// message flags, addrinfo flags, EAI codes and interface flags have other numbers. addrinfo,
// hostent and ifaddrs lists handed to the game are ours (Darwin sockaddrs), freed by our free*.
// Failures leave bionic's errno set; ___error translates it.
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>

#include "darwin.h"

static_assert(darwin::kSOCK_STREAM == SOCK_STREAM && darwin::kSOCK_DGRAM == SOCK_DGRAM && darwin::kSOCK_RAW == SOCK_RAW &&
                  darwin::kSOCK_SEQPACKET == SOCK_SEQPACKET,
              "socket types");
static_assert(darwin::kIPPROTO_IP == IPPROTO_IP && darwin::kIPPROTO_IPV6 == IPPROTO_IPV6 &&
                  darwin::kIPPROTO_TCP == IPPROTO_TCP && darwin::kIPPROTO_UDP == IPPROTO_UDP,
              "protocol numbers");
static_assert(darwin::kSHUT_RD == SHUT_RD && darwin::kSHUT_WR == SHUT_WR && darwin::kSHUT_RDWR == SHUT_RDWR, "shutdown");
static_assert(darwin::kTCP_NODELAY == TCP_NODELAY, "TCP_NODELAY");
static_assert(darwin::kNI_NOFQDN == NI_NOFQDN && darwin::kNI_NUMERICHOST == NI_NUMERICHOST &&
                  darwin::kNI_NAMEREQD == NI_NAMEREQD && darwin::kNI_NUMERICSERV == NI_NUMERICSERV && darwin::kNI_DGRAM == NI_DGRAM,
              "getnameinfo flags");
static_assert(darwin::kSizeof_fd_set == sizeof(fd_set) && darwin::kFD_SETSIZE == FD_SETSIZE, "fd_set (select passes through)");
static_assert(darwin::kPOLLIN == POLLIN && darwin::kPOLLPRI == POLLPRI && darwin::kPOLLOUT == POLLOUT &&
                  darwin::kPOLLERR == POLLERR && darwin::kPOLLHUP == POLLHUP && darwin::kPOLLNVAL == POLLNVAL &&
                  darwin::kSizeof_pollfd == sizeof(struct pollfd),
              "poll events used by the game");
static_assert(darwin::kSizeof_addrinfo == sizeof(struct addrinfo) &&
                  darwin::kOffsetof_addrinfo_ai_canonname == offsetof(struct addrinfo, ai_canonname) &&
                  darwin::kOffsetof_addrinfo_ai_addr == offsetof(struct addrinfo, ai_addr),
              "addrinfo: bionic's member order is BSD's");
static_assert(darwin::kSizeof_hostent == sizeof(struct hostent) &&
                  darwin::kOffsetof_hostent_h_addrtype == offsetof(struct hostent, h_addrtype),
              "hostent");
static_assert(darwin::kSizeof_ifaddrs == sizeof(struct ifaddrs) && darwin::kOffsetof_ifaddrs_ifa_data == offsetof(struct ifaddrs, ifa_data),
              "ifaddrs");
static_assert(darwin::kSizeof_sockaddr_in == sizeof(sockaddr_in) && darwin::kSizeof_sockaddr_in6 == sizeof(sockaddr_in6) &&
                  darwin::kOffsetof_sockaddr_in6_sin6_addr == offsetof(sockaddr_in6, sin6_addr),
              "inet addresses: same after the first two bytes");
static_assert(darwin::kSizeof_linger == sizeof(struct linger) && darwin::kSizeof_ip_mreq == sizeof(struct ip_mreq) &&
                  darwin::kSizeof_ipv6_mreq == sizeof(struct ipv6_mreq),
              "socket option values with the same layout");

namespace {

// Darwin's struct timeval: a 32-bit tv_usec followed by 4 bytes of padding the game may leave
// uninitialized (bionic's tv_usec is 64-bit): never handed to bionic as it is.
struct DarwinTimeval {
  long tv_sec;
  int32_t tv_usec;
  int32_t padding;
};
static_assert(sizeof(DarwinTimeval) == darwin::kSizeof_timeval && darwin::kSizeof_timeval_tv_usec == 4, "timeval");

timeval to_bionic(const DarwinTimeval &d) { return timeval{d.tv_sec, d.tv_usec}; }
DarwinTimeval to_darwin(const timeval &b) { return DarwinTimeval{b.tv_sec, static_cast<int32_t>(b.tv_usec), 0}; }

int to_bionic_family(int d) {
  if (d == darwin::kAF_INET6) return AF_INET6;
  if (d == darwin::kAF_INET) return AF_INET;
  if (d == darwin::kAF_UNIX) return AF_UNIX;
  if (d == darwin::kAF_UNSPEC) return AF_UNSPEC;
  return -1;
}

int to_darwin_family(int b) {
  if (b == AF_INET6) return static_cast<int>(darwin::kAF_INET6);
  if (b == AF_INET) return static_cast<int>(darwin::kAF_INET);
  if (b == AF_UNIX) return static_cast<int>(darwin::kAF_UNIX);
  if (b == AF_UNSPEC) return static_cast<int>(darwin::kAF_UNSPEC);
  return -1;
}

// Darwin sockaddr (from the game) -> bionic. false (EAFNOSUPPORT/EINVAL in errno) if unusable.
bool to_bionic(const void *darwin_addr, socklen_t darwin_len, sockaddr_storage *out, socklen_t *out_len) {
  const uint8_t *d = static_cast<const uint8_t *>(darwin_addr);
  if (!d || darwin_len < 2) {
    errno = EINVAL;
    return false;
  }
  int family = to_bionic_family(d[1]);
  if (family < 0) {
    errno = EAFNOSUPPORT;
    return false;
  }
  memset(out, 0, sizeof *out);
  size_t body = darwin_len - 2;
  size_t room = (family == AF_UNIX ? sizeof(sockaddr_un) : sizeof *out) - 2;
  if (body > room) body = room;
  memcpy(reinterpret_cast<uint8_t *>(out) + 2, d + 2, body);
  out->ss_family = static_cast<sa_family_t>(family);
  *out_len = static_cast<socklen_t>(2 + body);
  return true;
}

// bionic sockaddr -> Darwin, into the game's buffer of *darwin_len bytes (truncated like the
// kernel does); *darwin_len becomes the full Darwin length.
void to_darwin(const sockaddr *b, socklen_t b_len, void *darwin_addr, socklen_t *darwin_len) {
  if (!darwin_addr || !darwin_len) return;
  if (b_len < 2) {  // no address (recvfrom on a stream, an unnamed peer)
    *darwin_len = 0;
    return;
  }
  uint8_t tmp[sizeof(sockaddr_storage)];
  memset(tmp, 0, sizeof tmp);
  size_t full;
  if (b->sa_family == AF_INET) full = sizeof(sockaddr_in);
  else if (b->sa_family == AF_INET6) full = sizeof(sockaddr_in6);
  else if (b->sa_family == AF_UNIX) full = b_len > 2 ? (b_len > static_cast<socklen_t>(darwin::kSizeof_sockaddr_un) ? darwin::kSizeof_sockaddr_un : b_len) : 2;
  else full = b_len < sizeof tmp ? b_len : sizeof tmp;
  if (b_len > 2) memcpy(tmp + 2, reinterpret_cast<const uint8_t *>(b) + 2, (b_len < full ? b_len : full) - 2);
  tmp[0] = static_cast<uint8_t>(full);
  tmp[1] = static_cast<uint8_t>(to_darwin_family(b->sa_family));
  size_t n = *darwin_len < full ? *darwin_len : full;
  memcpy(darwin_addr, tmp, n);
  *darwin_len = static_cast<socklen_t>(full);
}

struct Pair { long darwin; int bionic; };
const Pair kSocketOptions[] = {
#define MCFM_PAIR(name, value) {value, name},
    MCFM_DARWIN_SOL_SOCKET_OPTIONS(MCFM_PAIR)
};
const Pair kIpOptions[] = {MCFM_DARWIN_IP_OPTIONS(MCFM_PAIR)};
const Pair kIpv6Options[] = {MCFM_DARWIN_IPV6_OPTIONS(MCFM_PAIR)};
const Pair kMessageFlags[] = {MCFM_DARWIN_MSG_FLAGS(MCFM_PAIR){darwin::kMSG_NOSIGNAL, MSG_NOSIGNAL}};
const Pair kEaiErrors[] = {MCFM_DARWIN_EAI_ERRORS(MCFM_PAIR)};
#undef MCFM_PAIR
const Pair kAiFlags[] = {
    {darwin::kAI_PASSIVE, AI_PASSIVE},         {darwin::kAI_CANONNAME, AI_CANONNAME},
    {darwin::kAI_NUMERICHOST, AI_NUMERICHOST}, {darwin::kAI_NUMERICSERV, AI_NUMERICSERV},
    {darwin::kAI_ADDRCONFIG, AI_ADDRCONFIG},
};  // AI_V4MAPPED and AI_ALL: bionic refuses them (EAI_BADFLAGS); getaddrinfo emulates them
const Pair kIfFlags[] = {
    {darwin::kIFF_UP, IFF_UP},           {darwin::kIFF_BROADCAST, IFF_BROADCAST}, {darwin::kIFF_LOOPBACK, IFF_LOOPBACK},
    {darwin::kIFF_POINTOPOINT, IFF_POINTOPOINT}, {darwin::kIFF_RUNNING, IFF_RUNNING}, {darwin::kIFF_MULTICAST, IFF_MULTICAST},
};

template <size_t N> int lookup(const Pair (&table)[N], long darwin_value) {
  for (const Pair &p : table)
    if (p.darwin == darwin_value) return p.bionic;
  return -1;
}

template <size_t N> int to_bionic_flags(const Pair (&table)[N], long d) {
  int b = 0;
  for (const Pair &p : table)
    if (d & p.darwin) b |= p.bionic;
  return b;
}

template <size_t N> int to_darwin_flags(const Pair (&table)[N], int b) {
  long d = 0;
  for (const Pair &p : table)
    if (b & p.bionic) d |= p.darwin;
  return static_cast<int>(d);
}

int eai_to_darwin(int b) {
  for (const Pair &p : kEaiErrors)
    if (p.bionic == b) return static_cast<int>(p.darwin);
  return b;
}

// SO_NOSIGPIPE has no bionic option: remembered per descriptor and applied as MSG_NOSIGNAL.
constexpr int kMaxFds = 65536;
std::atomic<bool> g_nosigpipe[kMaxFds];
bool nosigpipe(int fd) { return fd >= 0 && fd < kMaxFds && g_nosigpipe[fd].load(std::memory_order_relaxed); }
int send_flags(int fd, int darwin_flags) {
  return to_bionic_flags(kMessageFlags, darwin_flags) | (nosigpipe(fd) ? MSG_NOSIGNAL : 0);
}

// Translates a socket option; false (ENOPROTOOPT) for one we do not know.
bool option(int darwin_level, int darwin_name, int *level, int *name) {
  if (darwin_level == darwin::kSOL_SOCKET) {
    *level = SOL_SOCKET;
    *name = lookup(kSocketOptions, darwin_name);
  } else if (darwin_level == IPPROTO_IP) {
    *level = IPPROTO_IP;
    *name = lookup(kIpOptions, darwin_name);
  } else if (darwin_level == IPPROTO_IPV6) {
    *level = IPPROTO_IPV6;
    *name = lookup(kIpv6Options, darwin_name);
  } else if (darwin_level == IPPROTO_TCP) {
    *level = IPPROTO_TCP;
    *name = darwin_name == darwin::kTCP_NODELAY ? TCP_NODELAY : darwin_name == darwin::kTCP_KEEPALIVE ? TCP_KEEPIDLE : -1;
  } else {
    *level = darwin_level;  // other protocols: same numbers
    *name = darwin_name;
    return true;
  }
  if (*name < 0) {
    mcfm_darwin_log_once("socket option not supported on Android");
    errno = ENOPROTOOPT;
    return false;
  }
  return true;
}

// Darwin's struct msghdr: 32-bit lengths where bionic has size_t.
struct DarwinMsghdr {
  void *name;
  socklen_t namelen;
  struct iovec *iov;
  int iovlen;
  void *control;
  socklen_t controllen;
  int flags;
};
static_assert(sizeof(DarwinMsghdr) == darwin::kSizeof_msghdr && offsetof(DarwinMsghdr, iov) == darwin::kOffsetof_msghdr_msg_iov &&
                  offsetof(DarwinMsghdr, control) == darwin::kOffsetof_msghdr_msg_control &&
                  offsetof(DarwinMsghdr, flags) == darwin::kOffsetof_msghdr_msg_flags,
              "msghdr");

extern "C" void mcfm_darwin_freeaddrinfo(addrinfo *list);

// bionic's list -> ours (Darwin sockaddrs); nullptr when out of memory.
addrinfo *copy_addrinfo(const addrinfo *b) {
  addrinfo *head = nullptr, **tail = &head;
  for (; b; b = b->ai_next) {
    addrinfo *d = static_cast<addrinfo *>(calloc(1, sizeof(addrinfo) + sizeof(sockaddr_storage)));
    if (!d) {
      mcfm_darwin_freeaddrinfo(head);
      return nullptr;
    }
    d->ai_flags = to_darwin_flags(kAiFlags, b->ai_flags);
    d->ai_family = to_darwin_family(b->ai_family);
    d->ai_socktype = b->ai_socktype;
    d->ai_protocol = b->ai_protocol;
    if (b->ai_addr) {
      d->ai_addr = reinterpret_cast<sockaddr *>(d + 1);
      socklen_t len = sizeof(sockaddr_storage);
      to_darwin(b->ai_addr, b->ai_addrlen, d->ai_addr, &len);
      d->ai_addrlen = len;
    }
    if (b->ai_canonname) d->ai_canonname = strdup(b->ai_canonname);
    *tail = d;
    tail = &d->ai_next;
  }
  return head;
}

// A heap hostent (getipnodebyname); freehostent frees it.
hostent *new_hostent(const char *name, int bionic_family, const void *address, size_t length) {
  hostent *h = static_cast<hostent *>(calloc(1, sizeof(hostent) + 4 * sizeof(char *) + length));
  if (!h) return nullptr;
  char **lists = reinterpret_cast<char **>(h + 1);
  char *addr = reinterpret_cast<char *>(lists + 4);
  memcpy(addr, address, length);
  h->h_name = strdup(name);
  h->h_aliases = lists;  // empty: lists[0] = nullptr
  h->h_addr_list = lists + 2;
  lists[2] = addr;
  h->h_addrtype = to_darwin_family(bionic_family);
  h->h_length = static_cast<int>(length);
  return h;
}

thread_local hostent t_hostent;  // gethostbyname's result, Darwin family

hostent *darwin_hostent(hostent *b) {
  if (!b) return nullptr;
  t_hostent = *b;
  t_hostent.h_addrtype = to_darwin_family(b->h_addrtype);
  return &t_hostent;
}

}  // namespace

extern "C" {

int mcfm_darwin_socket(int domain, int type, int protocol) {
  int family = to_bionic_family(domain);
  if (family < 0) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  int fd = socket(family, type, protocol);
  if (fd >= 0 && fd < kMaxFds) g_nosigpipe[fd].store(false);
  return fd;
}

int mcfm_darwin_close(int fd) {
  if (fd >= 0 && fd < kMaxFds) g_nosigpipe[fd].store(false, std::memory_order_relaxed);
  return close(fd);
}

int mcfm_darwin_bind(int fd, const void *addr, socklen_t len) {
  sockaddr_storage b;
  socklen_t blen;
  return to_bionic(addr, len, &b, &blen) ? bind(fd, reinterpret_cast<sockaddr *>(&b), blen) : -1;
}

int mcfm_darwin_connect(int fd, const void *addr, socklen_t len) {
  sockaddr_storage b;
  socklen_t blen;
  return to_bionic(addr, len, &b, &blen) ? connect(fd, reinterpret_cast<sockaddr *>(&b), blen) : -1;
}

int mcfm_darwin_accept(int fd, void *addr, socklen_t *len) {
  sockaddr_storage b;
  socklen_t blen = sizeof b;
  int r = accept(fd, reinterpret_cast<sockaddr *>(&b), &blen);
  if (r >= 0) {
    if (r < kMaxFds) g_nosigpipe[r].store(false);
    if (addr && len) to_darwin(reinterpret_cast<sockaddr *>(&b), blen, addr, len);
  }
  return r;
}

int mcfm_darwin_getsockname(int fd, void *addr, socklen_t *len) {
  sockaddr_storage b;
  socklen_t blen = sizeof b;
  int r = getsockname(fd, reinterpret_cast<sockaddr *>(&b), &blen);
  if (r == 0) to_darwin(reinterpret_cast<sockaddr *>(&b), blen, addr, len);
  return r;
}

int mcfm_darwin_getpeername(int fd, void *addr, socklen_t *len) {
  sockaddr_storage b;
  socklen_t blen = sizeof b;
  int r = getpeername(fd, reinterpret_cast<sockaddr *>(&b), &blen);
  if (r == 0) to_darwin(reinterpret_cast<sockaddr *>(&b), blen, addr, len);
  return r;
}

int mcfm_darwin_setsockopt(int fd, int darwin_level, int darwin_name, const void *value, socklen_t len) {
  if (darwin_level == darwin::kSOL_SOCKET && darwin_name == darwin::kSO_NOSIGPIPE) {
    if (fd < 0 || fd >= kMaxFds || !value || len < sizeof(int)) {
      errno = EINVAL;
      return -1;
    }
    g_nosigpipe[fd].store(*static_cast<const int *>(value) != 0);
    return 0;
  }
  int level, name;
  if (!option(darwin_level, darwin_name, &level, &name)) return -1;
  if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO)) {
    if (!value || len < sizeof(DarwinTimeval)) {
      errno = EINVAL;
      return -1;
    }
    timeval b = to_bionic(*static_cast<const DarwinTimeval *>(value));
    return setsockopt(fd, level, name, &b, sizeof b);
  }
  return setsockopt(fd, level, name, value, len);
}

int mcfm_darwin_getsockopt(int fd, int darwin_level, int darwin_name, void *value, socklen_t *len) {
  if (darwin_level == darwin::kSOL_SOCKET && darwin_name == darwin::kSO_NOSIGPIPE) {
    if (!value || !len || *len < sizeof(int)) {
      errno = EINVAL;
      return -1;
    }
    *static_cast<int *>(value) = nosigpipe(fd);
    *len = sizeof(int);
    return 0;
  }
  int level, name;
  if (!option(darwin_level, darwin_name, &level, &name)) return -1;
  if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO)) {
    if (!value || !len || *len < sizeof(DarwinTimeval)) {
      errno = EINVAL;
      return -1;
    }
    timeval b;
    socklen_t blen = sizeof b;
    int r = getsockopt(fd, level, name, &b, &blen);
    if (r == 0) {
      *static_cast<DarwinTimeval *>(value) = to_darwin(b);
      *len = sizeof(DarwinTimeval);
    }
    return r;
  }
  int r = getsockopt(fd, level, name, value, len);
  if (r == 0 && level == SOL_SOCKET && name == SO_ERROR && value) *static_cast<int *>(value) = mcfm_darwin_errno(*static_cast<int *>(value));
  return r;
}

ssize_t mcfm_darwin_send(int fd, const void *buf, size_t n, int flags) { return send(fd, buf, n, send_flags(fd, flags)); }

ssize_t mcfm_darwin_sendto(int fd, const void *buf, size_t n, int flags, const void *addr, socklen_t len) {
  if (!addr) return sendto(fd, buf, n, send_flags(fd, flags), nullptr, 0);
  sockaddr_storage b;
  socklen_t blen;
  return to_bionic(addr, len, &b, &blen) ? sendto(fd, buf, n, send_flags(fd, flags), reinterpret_cast<sockaddr *>(&b), blen) : -1;
}

ssize_t mcfm_darwin_recv(int fd, void *buf, size_t n, int flags) { return recv(fd, buf, n, to_bionic_flags(kMessageFlags, flags)); }

ssize_t mcfm_darwin_recvfrom(int fd, void *buf, size_t n, int flags, void *addr, socklen_t *len) {
  sockaddr_storage b;
  socklen_t blen = sizeof b;
  ssize_t r = recvfrom(fd, buf, n, to_bionic_flags(kMessageFlags, flags), reinterpret_cast<sockaddr *>(&b), &blen);
  if (r >= 0 && addr && len) to_darwin(reinterpret_cast<sockaddr *>(&b), blen, addr, len);
  return r;
}

// Control messages (ancillary data) are not translated: dropped, logged once.
ssize_t mcfm_darwin_sendmsg(int fd, const DarwinMsghdr *d, int flags) {
  msghdr b;
  memset(&b, 0, sizeof b);
  sockaddr_storage name;
  socklen_t name_len = 0;
  if (d->name && !to_bionic(d->name, d->namelen, &name, &name_len)) return -1;
  b.msg_name = d->name ? &name : nullptr;
  b.msg_namelen = name_len;
  b.msg_iov = d->iov;
  b.msg_iovlen = static_cast<size_t>(d->iovlen);
  if (d->control && d->controllen) mcfm_darwin_log_once("sendmsg: control messages dropped");
  return sendmsg(fd, &b, send_flags(fd, flags));
}

ssize_t mcfm_darwin_recvmsg(int fd, DarwinMsghdr *d, int flags) {
  msghdr b;
  memset(&b, 0, sizeof b);
  sockaddr_storage name;
  b.msg_name = d->name ? &name : nullptr;
  b.msg_namelen = d->name ? sizeof name : 0;
  b.msg_iov = d->iov;
  b.msg_iovlen = static_cast<size_t>(d->iovlen);
  ssize_t r = recvmsg(fd, &b, to_bionic_flags(kMessageFlags, flags));
  if (r < 0) return r;
  if (d->name) to_darwin(reinterpret_cast<sockaddr *>(&name), b.msg_namelen, d->name, &d->namelen);
  if (d->control && d->controllen) mcfm_darwin_log_once("recvmsg: control messages dropped");
  d->controllen = 0;
  d->flags = to_darwin_flags(kMessageFlags, b.msg_flags);
  return r;
}

// write() honours SO_NOSIGPIPE on Darwin.
ssize_t mcfm_darwin_write(int fd, const void *buf, size_t n) {
  return nosigpipe(fd) ? send(fd, buf, n, MSG_NOSIGNAL) : write(fd, buf, n);
}

// Darwin's select leaves the timeout as it was (Linux updates it).
int mcfm_darwin_select(int n, fd_set *r, fd_set *w, fd_set *e, DarwinTimeval *timeout) {
  if (!timeout) return select(n, r, w, e, nullptr);
  timeval b = to_bionic(*timeout);
  return select(n, r, w, e, &b);
}

int mcfm_darwin_poll(struct pollfd *fds, nfds_t n, int timeout) {
  // POLLWRNORM/POLLWRBAND differ (Darwin 0x4/0x100, bionic 0x100/0x200); Darwin's POLLEXTEND
  // and the like (0x200 and up) have no bionic counterpart; the rest is the same.
  for (nfds_t i = 0; i < n; i++) {
    short e = fds[i].events;
    short b = static_cast<short>(e & 0xFF);
    if (e & 0x100) b |= POLLWRBAND;
    fds[i].events = b;
  }
  int r = poll(fds, n, timeout);
  for (nfds_t i = 0; i < n; i++) {
    short e = fds[i].events, re = fds[i].revents;
    fds[i].events = static_cast<short>((e & ~POLLWRBAND) | ((e & POLLWRBAND) ? 0x100 : 0));
    short d = static_cast<short>(re & ~(POLLWRNORM | POLLWRBAND));
    if (re & POLLWRNORM) d |= POLLOUT;
    if (re & POLLWRBAND) d |= 0x100;
    fds[i].revents = d;
  }
  return r;
}

// IPv4 results as IPv4-mapped IPv6 addresses (::ffff:a.b.c.d), for AI_V4MAPPED.
void map_v4(addrinfo *list) {
  for (addrinfo *ai = list; ai; ai = ai->ai_next) {
    if (ai->ai_family != static_cast<int>(darwin::kAF_INET) || !ai->ai_addr) continue;
    uint8_t *d = reinterpret_cast<uint8_t *>(ai->ai_addr);  // room for a sockaddr_storage
    uint8_t port[2], v4[4];
    memcpy(port, d + 2, 2);
    memcpy(v4, d + 4, 4);
    memset(d, 0, sizeof(sockaddr_in6));
    d[0] = sizeof(sockaddr_in6);
    d[1] = static_cast<uint8_t>(darwin::kAF_INET6);
    memcpy(d + 2, port, 2);
    d[8 + 10] = 0xff;
    d[8 + 11] = 0xff;
    memcpy(d + 8 + 12, v4, 4);
    ai->ai_family = static_cast<int>(darwin::kAF_INET6);
    ai->ai_addrlen = sizeof(sockaddr_in6);
  }
}

addrinfo *append(addrinfo *a, addrinfo *b) {
  if (!a) return b;
  addrinfo *t = a;
  while (t->ai_next) t = t->ai_next;
  t->ai_next = b;
  return a;
}

int lookup_darwin(const char *node, const char *service, const addrinfo *hints, int bionic_family, addrinfo **out) {
  addrinfo b;
  memset(&b, 0, sizeof b);
  if (hints) {
    b.ai_flags = to_bionic_flags(kAiFlags, hints->ai_flags);
    b.ai_socktype = hints->ai_socktype;
    b.ai_protocol = hints->ai_protocol;
  }
  b.ai_family = bionic_family;
  addrinfo *list = nullptr;
  int r = getaddrinfo(node, service, &b, &list);
  if (r != 0) return eai_to_darwin(r);
  *out = copy_addrinfo(list);
  freeaddrinfo(list);
  return *out ? 0 : eai_to_darwin(EAI_MEMORY);
}

// AI_V4MAPPED with an AF_INET6 hint: IPv6 results, or the IPv4 ones mapped when there are none;
// with AI_ALL both. Darwin ignores both flags for other families.
int mcfm_darwin_getaddrinfo(const char *node, const char *service, const addrinfo *hints, addrinfo **res) {
  int family = hints ? to_bionic_family(hints->ai_family) : AF_UNSPEC;
  if (family < 0) return eai_to_darwin(EAI_FAMILY);
  bool v4mapped = hints && family == AF_INET6 && (hints->ai_flags & darwin::kAI_V4MAPPED);
  if (!v4mapped) return lookup_darwin(node, service, hints, family, res);
  bool all = (hints->ai_flags & darwin::kAI_ALL) != 0;
  addrinfo *v6 = nullptr, *v4 = nullptr;
  int r6 = lookup_darwin(node, service, hints, AF_INET6, &v6);
  if (r6 == 0 && !all) {
    *res = v6;
    return 0;
  }
  int r4 = lookup_darwin(node, service, hints, AF_INET, &v4);
  if (r4 == 0) map_v4(v4);
  if (!v6 && !v4) return r6;
  *res = append(v6, v4);
  return 0;
}

void mcfm_darwin_freeaddrinfo(addrinfo *list) {
  while (list) {
    addrinfo *next = list->ai_next;
    free(list->ai_canonname);
    free(list);
    list = next;
  }
}

int mcfm_darwin_getnameinfo(const void *addr, socklen_t len, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags) {
  sockaddr_storage b;
  socklen_t blen;
  if (!to_bionic(addr, len, &b, &blen)) return eai_to_darwin(EAI_FAMILY);
  int r = getnameinfo(reinterpret_cast<sockaddr *>(&b), blen, host, hostlen, serv, servlen, flags);
  return r == 0 ? 0 : eai_to_darwin(r);
}

hostent *mcfm_darwin_gethostbyname(const char *name) { return darwin_hostent(gethostbyname(name)); }

hostent *mcfm_darwin_gethostbyaddr(const void *addr, socklen_t len, int type) {
  return darwin_hostent(gethostbyaddr(addr, len, to_bionic_family(type)));
}

hostent *mcfm_darwin_getipnodebyname(const char *name, int af, int, int *error) {
  addrinfo hints, *list = nullptr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = to_bionic_family(af);
  hints.ai_socktype = SOCK_STREAM;
  if (hints.ai_family < 0 || getaddrinfo(name, nullptr, &hints, &list) != 0 || !list) {
    if (error) *error = 1;  // HOST_NOT_FOUND
    return nullptr;
  }
  const void *address = list->ai_family == AF_INET6
                            ? static_cast<const void *>(&reinterpret_cast<sockaddr_in6 *>(list->ai_addr)->sin6_addr)
                            : static_cast<const void *>(&reinterpret_cast<sockaddr_in *>(list->ai_addr)->sin_addr);
  hostent *h = new_hostent(name, list->ai_family, address, list->ai_family == AF_INET6 ? 16 : 4);
  freeaddrinfo(list);
  if (error) *error = h ? 0 : 1;
  return h;
}

void mcfm_darwin_freehostent(hostent *h) {
  if (!h) return;
  free(h->h_name);
  free(h);
}

void mcfm_darwin_freeifaddrs(ifaddrs *list);

int mcfm_darwin_getifaddrs(ifaddrs **out) {
  ifaddrs *list = nullptr;
  if (getifaddrs(&list) != 0) return -1;
  ifaddrs *head = nullptr, **tail = &head;
  for (ifaddrs *b = list; b; b = b->ifa_next) {
    // Link-layer (AF_PACKET) entries have no Darwin counterpart here (Darwin: AF_LINK).
    if (b->ifa_addr && to_darwin_family(b->ifa_addr->sa_family) < 0) continue;
    size_t name_len = strlen(b->ifa_name) + 1;
    ifaddrs *d = static_cast<ifaddrs *>(calloc(1, sizeof(ifaddrs) + 3 * sizeof(sockaddr_storage) + name_len));
    if (!d) {
      freeifaddrs(list);
      mcfm_darwin_freeifaddrs(head);
      errno = ENOMEM;
      return -1;
    }
    sockaddr_storage *space = reinterpret_cast<sockaddr_storage *>(d + 1);
    d->ifa_name = reinterpret_cast<char *>(space + 3);
    memcpy(d->ifa_name, b->ifa_name, name_len);
    d->ifa_flags = static_cast<unsigned>(to_darwin_flags(kIfFlags, static_cast<int>(b->ifa_flags)));
    const sockaddr *from[3] = {b->ifa_addr, b->ifa_netmask, b->ifa_broadaddr};
    sockaddr **to[3] = {&d->ifa_addr, &d->ifa_netmask, &d->ifa_broadaddr};
    for (int i = 0; i < 3; i++)
      if (from[i] && to_darwin_family(from[i]->sa_family) >= 0) {
        socklen_t len = sizeof(sockaddr_storage);
        to_darwin(from[i], from[i]->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_storage), &space[i], &len);
        *to[i] = reinterpret_cast<sockaddr *>(&space[i]);
      }
    *tail = d;
    tail = &d->ifa_next;
  }
  freeifaddrs(list);
  *out = head;
  return 0;
}

void mcfm_darwin_freeifaddrs(ifaddrs *list) {
  while (list) {
    ifaddrs *next = list->ifa_next;
    free(list);
    list = next;
  }
}

int mcfm_darwin_inet_pton(int af, const char *src, void *dst) {
  int b = to_bionic_family(af);
  if (b < 0) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  return inet_pton(b, src, dst);
}

const char *mcfm_darwin_inet_ntop(int af, const void *src, char *dst, socklen_t size) {
  int b = to_bionic_family(af);
  if (b < 0) {
    errno = EAFNOSUPPORT;
    return nullptr;
  }
  return inet_ntop(b, src, dst, size);
}

}  // extern "C"
