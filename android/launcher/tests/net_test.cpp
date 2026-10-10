// Darwin socket shims on Android (docs/LAUNCHER.md, Stage 3a): addresses handed back to the game
// are Darwin sockaddrs, truncated to the game's buffer (never written past) with the full length
// reported, as the kernel does.
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "darwin.h"

extern "C" {
int mcfm_darwin_socket(int, int, int);
int mcfm_darwin_bind(int, const void *, socklen_t);
int mcfm_darwin_getsockname(int, void *, socklen_t *);
int mcfm_darwin_close(int);
}

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  int fd = mcfm_darwin_socket(static_cast<int>(darwin::kAF_INET6), SOCK_DGRAM, 0);
  EXPECT(fd >= 0);
  unsigned char d[28];  // Darwin sockaddr_in6: len, family 30, port, flowinfo, addr (::1), scope
  memset(d, 0, sizeof d);
  d[0] = 28;
  d[1] = static_cast<unsigned char>(darwin::kAF_INET6);
  d[23] = 1;
  EXPECT(mcfm_darwin_bind(fd, d, sizeof d) == 0);
  unsigned char out[8 + 32];
  memset(out, 0xA5, sizeof out);
  socklen_t len = 8;
  EXPECT(mcfm_darwin_getsockname(fd, out, &len) == 0);
  EXPECT(len == 28);                                          // full Darwin length reported
  EXPECT(out[0] == 28 && out[1] == darwin::kAF_INET6);         // Darwin header
  bool guard = true;
  for (size_t i = 8; i < sizeof out; i++) guard &= out[i] == 0xA5;
  EXPECT(guard);                                               // nothing past the 8 bytes given
  EXPECT(mcfm_darwin_close(fd) == 0);
  // An unknown Darwin family is refused, not passed to the kernel.
  d[1] = 99;
  fd = mcfm_darwin_socket(static_cast<int>(darwin::kAF_INET6), SOCK_DGRAM, 0);
  EXPECT(mcfm_darwin_bind(fd, d, sizeof d) == -1);
  mcfm_darwin_close(fd);
  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("net_test: all passed\n");
  return 0;
}
