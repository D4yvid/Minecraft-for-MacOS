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
int mcfm_darwin_connect(int, const void *, socklen_t);
int mcfm_darwin_accept(int, void *, socklen_t *);
ssize_t mcfm_darwin_recvfrom(int, void *, size_t, int, void *, socklen_t *);
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
  // recvfrom on a connected TCP socket has no address: the length reported is 0.
  int server = mcfm_darwin_socket(static_cast<int>(darwin::kAF_INET), SOCK_STREAM, 0);
  unsigned char v4[16] = {16, static_cast<unsigned char>(darwin::kAF_INET), 0, 0, 127, 0, 0, 1};
  EXPECT(mcfm_darwin_bind(server, v4, sizeof v4) == 0 && listen(server, 1) == 0);
  socklen_t blen = sizeof v4;
  mcfm_darwin_getsockname(server, v4, &blen);
  int client = mcfm_darwin_socket(static_cast<int>(darwin::kAF_INET), SOCK_STREAM, 0);
  EXPECT(mcfm_darwin_connect(client, v4, sizeof v4) == 0);
  int peer = mcfm_darwin_accept(server, nullptr, nullptr);
  EXPECT(write(client, "x", 1) == 1);
  unsigned char from[16];
  memset(from, 0xA5, sizeof from);
  socklen_t flen = sizeof from;
  char byte;
  EXPECT(mcfm_darwin_recvfrom(peer, &byte, 1, 0, from, &flen) == 1);
  EXPECT(flen == 0 && from[0] == 0xA5);
  mcfm_darwin_close(peer);
  mcfm_darwin_close(client);
  mcfm_darwin_close(server);

  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("net_test: all passed\n");
  return 0;
}
