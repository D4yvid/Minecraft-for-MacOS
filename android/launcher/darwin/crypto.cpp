// The CommonCrypto subset the game imports: SHA-256 (FIPS 180-4) in Darwin's CC_SHA256_CTX
// layout, HMAC-SHA256 (RFC 2104) in a CCHmacContext, and CCCrypt, which reports
// kCCUnimplemented: it is used by the Xbox Live code the launcher does not run.
#include <stdint.h>
#include <string.h>

#include "darwin.h"

namespace {

// Darwin's CC_SHA256_CTX: bit count (low, high), state, 64-byte block buffer.
struct Sha256 {
  uint32_t count[2];
  uint32_t hash[8];
  uint32_t wbuf[16];
};
static_assert(sizeof(Sha256) == darwin::kSizeof_CC_SHA256_CTX, "CC_SHA256_CTX");

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void compress(Sha256 *s, const uint8_t *block) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) | (uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s->hash[0], b = s->hash[1], c = s->hash[2], d = s->hash[3], e = s->hash[4], f = s->hash[5], g = s->hash[6],
           h = s->hash[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  s->hash[0] += a;
  s->hash[1] += b;
  s->hash[2] += c;
  s->hash[3] += d;
  s->hash[4] += e;
  s->hash[5] += f;
  s->hash[6] += g;
  s->hash[7] += h;
}

void sha_init(Sha256 *s) {
  static const uint32_t kInit[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memset(s, 0, sizeof *s);
  memcpy(s->hash, kInit, sizeof kInit);
}

void sha_update(Sha256 *s, const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint8_t *buffer = reinterpret_cast<uint8_t *>(s->wbuf);
  size_t used = (s->count[0] >> 3) & 63;
  uint64_t bits = (uint64_t(s->count[1]) << 32 | s->count[0]) + (uint64_t(len) << 3);
  s->count[0] = static_cast<uint32_t>(bits);
  s->count[1] = static_cast<uint32_t>(bits >> 32);
  while (len > 0) {
    size_t n = 64 - used < len ? 64 - used : len;
    memcpy(buffer + used, p, n);
    used += n;
    p += n;
    len -= n;
    if (used == 64) {
      compress(s, buffer);
      used = 0;
    }
  }
}

void sha_final(uint8_t *digest, Sha256 *s) {
  uint64_t bits = uint64_t(s->count[1]) << 32 | s->count[0];
  uint8_t pad[72] = {0x80};
  size_t used = (bits >> 3) & 63;
  size_t pad_len = used < 56 ? 56 - used : 120 - used;
  uint8_t length[8];
  for (int i = 0; i < 8; i++) length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  sha_update(s, pad, pad_len);
  sha_update(s, length, 8);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 4; j++) digest[4 * i + j] = static_cast<uint8_t>(s->hash[i] >> (24 - 8 * j));
  memset(s, 0, sizeof *s);
}

// CCHmacContext (384 bytes): the inner and outer hashes and the algorithm.
struct Hmac {
  Sha256 inner, outer;
  int algorithm;
};
static_assert(sizeof(Hmac) <= darwin::kSizeof_CCHmacContext, "CCHmacContext");

constexpr int kCCUnimplemented = -4305;

}  // namespace

extern "C" {

int mcfm_darwin_CC_SHA256_Init(void *c) {
  sha_init(static_cast<Sha256 *>(c));
  return 1;
}

int mcfm_darwin_CC_SHA256_Update(void *c, const void *data, uint32_t len) {
  sha_update(static_cast<Sha256 *>(c), data, len);
  return 1;
}

int mcfm_darwin_CC_SHA256_Final(unsigned char *digest, void *c) {
  sha_final(digest, static_cast<Sha256 *>(c));
  return 1;
}

void mcfm_darwin_CCHmacInit(void *context, uint32_t algorithm, const void *key, size_t key_length) {
  Hmac *h = static_cast<Hmac *>(context);
  h->algorithm = static_cast<int>(algorithm);
  if (algorithm != darwin::kCCHmacAlgSHA256) {
    mcfm_darwin_log_once("CCHmac: only SHA-256 is supported");
    return;
  }
  uint8_t block[64] = {0};
  if (key_length > 64) {
    Sha256 k;
    sha_init(&k);
    sha_update(&k, key, key_length);
    sha_final(block, &k);
  } else {
    memcpy(block, key, key_length);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = block[i] ^ 0x36;
    opad[i] = block[i] ^ 0x5c;
  }
  sha_init(&h->inner);
  sha_update(&h->inner, ipad, 64);
  sha_init(&h->outer);
  sha_update(&h->outer, opad, 64);
}

void mcfm_darwin_CCHmacUpdate(void *context, const void *data, size_t length) {
  Hmac *h = static_cast<Hmac *>(context);
  if (h->algorithm == darwin::kCCHmacAlgSHA256) sha_update(&h->inner, data, length);
}

void mcfm_darwin_CCHmacFinal(void *context, void *mac) {
  Hmac *h = static_cast<Hmac *>(context);
  if (h->algorithm != darwin::kCCHmacAlgSHA256) {
    memset(mac, 0, 32);
    return;
  }
  uint8_t inner[32];
  sha_final(inner, &h->inner);
  sha_update(&h->outer, inner, sizeof inner);
  sha_final(static_cast<uint8_t *>(mac), &h->outer);
}

int mcfm_darwin_CCCrypt(int, int, int, const void *, size_t, const void *, const void *, size_t, void *, size_t, size_t *moved) {
  mcfm_darwin_log_once("CCCrypt: not supported (Xbox Live code only)");
  if (moved) *moved = 0;
  return kCCUnimplemented;
}

}  // extern "C"
