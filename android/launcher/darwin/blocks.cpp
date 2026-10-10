// The blocks runtime (clang's Block ABI, "Block Implementation Specification"): what Darwin code
// compiled with blocks needs from libSystem. _Block_object_assign/_dispose are called by the
// copy/dispose helpers the compiler generates; _Block_copy/_Block_release are used by dispatch.
// Captured Objective-C objects are not retained: the ObjC runtime is stubbed.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "darwin.h"

namespace {

enum : int32_t {
  kDeallocating = 0x1,
  kRefcountMask = 0xfffe,  // counted in steps of 2
  kNeedsFree = 1 << 24,
  kHasCopyDispose = 1 << 25,
  kIsGlobal = 1 << 28,
  kByrefLayoutExtended = 1 << 28,  // same bit, on __block variables
};
enum : int {  // _Block_object_assign / _dispose field kinds
  kFieldIsObject = 3,
  kFieldIsBlock = 7,
  kFieldIsByref = 8,
  kFieldIsWeak = 16,
  kByrefCaller = 128,
};

struct Descriptor {
  unsigned long reserved, size;
  void (*copy)(void *dst, const void *src);  // with kHasCopyDispose
  void (*dispose)(const void *);
};

struct Block {
  void *isa;
  std::atomic<int32_t> flags;
  int32_t reserved;
  void (*invoke)(void *, ...);
  Descriptor *descriptor;
};

struct Byref {
  void *isa;
  Byref *forwarding;
  std::atomic<int32_t> flags;
  uint32_t size;
  // with kHasCopyDispose:
  void (*keep)(Byref *dst, Byref *src);
  void (*destroy)(Byref *);
};

// Increments the count unless it saturated; returns false if the object is deallocating.
void retain(std::atomic<int32_t> &flags) {
  int32_t old = flags.load();
  while ((old & kRefcountMask) != kRefcountMask && !flags.compare_exchange_weak(old, old + 2)) {}
}

// Decrements; true when the last reference went away.
bool release(std::atomic<int32_t> &flags) {
  int32_t old = flags.load();
  for (;;) {
    if ((old & kRefcountMask) == kRefcountMask) return false;  // saturated: never freed
    int32_t next = old - 2;
    bool last = (old & kRefcountMask) == 2;
    if (last) next = (old & ~kRefcountMask) | kDeallocating;
    if (flags.compare_exchange_weak(old, next)) return last;
  }
}

Byref *byref_copy(Byref *src) {
  Byref *from = src->forwarding;
  if (from->flags.load() & kNeedsFree) {  // already on the heap
    retain(from->flags);
    return from;
  }
  Byref *copy = static_cast<Byref *>(malloc(from->size));
  copy->isa = nullptr;
  copy->flags.store((from->flags.load() & ~kRefcountMask) | kNeedsFree | 4);  // the heap copy and the stack
  copy->forwarding = copy;
  from->forwarding = copy;
  copy->size = from->size;
  if (from->flags.load() & kHasCopyDispose) {
    // The helpers, then the extended layout word (if any); keep copies the variable itself.
    size_t header = offsetof(Byref, destroy) + sizeof(void (*)(Byref *));
    if (from->flags.load() & kByrefLayoutExtended) header += sizeof(void *);
    memmove(reinterpret_cast<char *>(copy) + offsetof(Byref, keep), reinterpret_cast<char *>(from) + offsetof(Byref, keep),
            header - offsetof(Byref, keep));
    from->keep(copy, from);
  } else {
    size_t header = offsetof(Byref, keep);
    memmove(reinterpret_cast<char *>(copy) + header, reinterpret_cast<char *>(from) + header, from->size - header);
  }
  return copy;
}

void byref_release(Byref *byref) {
  byref = byref->forwarding;
  if (!(byref->flags.load() & kNeedsFree)) return;  // still on the stack
  if (release(byref->flags)) {
    if (byref->flags.load() & kHasCopyDispose) byref->destroy(byref);
    free(byref);
  }
}

}  // namespace

extern "C" {

// isa values: only their addresses matter (no ObjC runtime looks at them).
void *mcfm_darwin_NSConcreteGlobalBlock[32];
void *mcfm_darwin_NSConcreteStackBlock[32];
void *mcfm_darwin_NSConcreteMallocBlock[32];

void *mcfm_darwin_Block_copy(const void *arg) {
  if (!arg) return nullptr;
  Block *b = static_cast<Block *>(const_cast<void *>(arg));
  int32_t flags = b->flags.load();
  if (flags & kNeedsFree) {
    retain(b->flags);
    return b;
  }
  if (flags & kIsGlobal) return b;
  Block *copy = static_cast<Block *>(malloc(b->descriptor->size));
  memmove(copy, b, b->descriptor->size);
  copy->flags.store((flags & ~(kRefcountMask | kDeallocating)) | kNeedsFree | 2);
  copy->isa = mcfm_darwin_NSConcreteMallocBlock;
  if (flags & kHasCopyDispose) b->descriptor->copy(copy, b);
  return copy;
}

void mcfm_darwin_Block_release(const void *arg) {
  if (!arg) return;
  Block *b = static_cast<Block *>(const_cast<void *>(arg));
  if (!(b->flags.load() & kNeedsFree)) return;  // global or stack
  if (release(b->flags)) {
    if (b->flags.load() & kHasCopyDispose) b->descriptor->dispose(b);
    free(b);
  }
}

void mcfm_darwin__Block_object_assign(void *dest, const void *object, int flags) {
  void **slot = static_cast<void **>(dest);
  if (flags & kByrefCaller) {  // called from a __block variable's own helpers: plain assignment
    *slot = const_cast<void *>(object);
    return;
  }
  switch (flags & ~kFieldIsWeak) {
    case kFieldIsBlock: *slot = mcfm_darwin_Block_copy(object); break;
    case kFieldIsByref: *slot = byref_copy(static_cast<Byref *>(const_cast<void *>(object))); break;
    case kFieldIsObject:
    default: *slot = const_cast<void *>(object); break;
  }
}

void mcfm_darwin__Block_object_dispose(const void *object, int flags) {
  if (flags & kByrefCaller) return;
  switch (flags & ~kFieldIsWeak) {
    case kFieldIsBlock: mcfm_darwin_Block_release(object); break;
    case kFieldIsByref: byref_release(static_cast<Byref *>(const_cast<void *>(object))); break;
    default: break;
  }
}

}  // extern "C"
