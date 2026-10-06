#pragma once
// Locating a virtual function in a vtable obtained with dlsym("_ZTV..."). Pure, C++11.

namespace mcfm {
namespace android {

// `vtable` is the _ZTV symbol: word 0 offset-to-top, word 1 typeinfo, then the virtual
// functions. Returns the word index of `needle` among the functions, or -1 when either
// pointer is null or `needle` is not in this vtable. The scan ends at the first null word
// (the next vtable's offset-to-top) and never goes past `maxWords`.
inline int find_vtable_index(void *const *vtable, const void *needle, int maxWords = 512) {
  if (!vtable || !needle) return -1;
  for (int i = 2; i < maxWords; i++) {
    if (!vtable[i]) return -1;
    if (vtable[i] == needle) return i;
  }
  return -1;
}

}  // namespace android
}  // namespace mcfm
