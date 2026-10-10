#pragma once
// Runtime for the generated framework stubs (tools/launcher/mcfm_image.py stubs).
// A stub returns 0 / nil and logs "mcfm: stub <lib>:<symbol>" the first time it is called;
// with MCFM_CENSUS=<file> the line is also appended to that file.
#ifdef __cplusplus
extern "C" {
#endif
void mcfm_stub_hit(int *seen, const char *lib, const char *symbol);
// objc_msgSend stubs: the selector is a C string (the image's selector refs are never
// registered with a runtime); logged once per selector.
void mcfm_stub_msgsend(const char *lib, const char *symbol, const char *selector);
#ifdef __cplusplus
}
#endif

#define MCFM_STUB_FN(n, lib, sym)                                  \
  void *mcfm_stub_##n(void) __asm__(sym);                          \
  void *mcfm_stub_##n(void) {                                      \
    static int seen;                                               \
    mcfm_stub_hit(&seen, lib, sym);                                \
    return 0;                                                      \
  }

#define MCFM_STUB_MSGSEND(n, lib, sym)                             \
  void *mcfm_stub_##n(void *self, const char *sel) __asm__(sym);  \
  void *mcfm_stub_##n(void *self, const char *sel) {              \
    (void)self;                                                    \
    mcfm_stub_msgsend(lib, sym, sel);                              \
    return 0;                                                      \
  }

#define MCFM_STUB_DATA(n, sym) \
  __attribute__((aligned(16))) char mcfm_data_##n[256] __asm__(sym) = {0};
