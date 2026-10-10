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

// Stubs return like a message to nil: x0, x1 (pointer, integer, two-register struct) and
// d0-d3 (float, double, CGRect-like structs) are zero. Structs returned through x8 (larger
// than 16 bytes, not floating-point) are left as the caller's memory was.
typedef struct { void *x0, *x1; } mcfm_stub_ret;
#define MCFM_ZERO_FP_RETURN() \
  __asm__ volatile("movi d0, #0\n movi d1, #0\n movi d2, #0\n movi d3, #0" ::: "d0", "d1", "d2", "d3")

#define MCFM_STUB_FN(n, lib, sym)                                          \
  mcfm_stub_ret mcfm_stub_##n(void) __asm__(sym);                          \
  mcfm_stub_ret mcfm_stub_##n(void) {                                      \
    static int seen;                                                       \
    mcfm_stub_hit(&seen, lib, sym);                                        \
    MCFM_ZERO_FP_RETURN();                                                 \
    mcfm_stub_ret r = {0, 0};                                              \
    return r;                                                              \
  }

#define MCFM_STUB_MSGSEND(n, lib, sym)                                     \
  mcfm_stub_ret mcfm_stub_##n(void *self, const char *sel) __asm__(sym);  \
  mcfm_stub_ret mcfm_stub_##n(void *self, const char *sel) {              \
    (void)self;                                                            \
    mcfm_stub_msgsend(lib, sym, sel);                                      \
    MCFM_ZERO_FP_RETURN();                                                 \
    mcfm_stub_ret r = {0, 0};                                              \
    return r;                                                              \
  }

#define MCFM_STUB_DATA(n, sym) \
  __attribute__((aligned(16))) char mcfm_data_##n[256] __asm__(sym) = {0};
