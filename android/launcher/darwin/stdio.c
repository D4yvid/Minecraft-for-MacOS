// Darwin variadic calls on bionic (docs/research/android-launcher.md): Darwin arm64 passes every
// variadic argument on the stack in 8-byte slots, AAPCS64 in registers first. Each shim is itself
// variadic: after va_start it marks the AAPCS64 va_list's register save areas exhausted
// (__gr_offs = __vr_offs = 0), so va_arg reads from __stack, the caller's stack pointer, which is
// exactly where Darwin put the arguments. A Darwin va_list (a char * into such a stack area)
// becomes an AAPCS64 va_list with __stack = ap and both offsets 0.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct aapcs64_va_list {
  void *stack, *gr_top, *vr_top;
  int gr_offs, vr_offs;
};
_Static_assert(sizeof(struct aapcs64_va_list) == sizeof(va_list), "AAPCS64 va_list");

static void stack_only(va_list *l) {
  struct aapcs64_va_list *v = (struct aapcs64_va_list *)l;
  v->gr_offs = 0;
  v->vr_offs = 0;
}

static void from_darwin(va_list *l, char *darwin_ap) {
  struct aapcs64_va_list v = {darwin_ap, 0, 0, 0, 0};
  memcpy(l, &v, sizeof v);
}

#define DARWIN_VA_BEGIN(last) \
  va_list l;                  \
  va_start(l, last);          \
  stack_only(&l)

int mcfm_darwin_printf(const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vprintf(format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_fprintf(FILE *f, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vfprintf(f, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_sprintf(char *out, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vsprintf(out, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_snprintf(char *out, size_t n, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vsnprintf(out, n, format, l);
  va_end(l);
  return r;
}

// Darwin's _FORTIFY_SOURCE entry: `slen` is the object size the compiler knew.
int mcfm_darwin___snprintf_chk(char *out, size_t n, int flag, size_t slen, const char *format, ...) {
  (void)flag;
  if (n > slen) abort();
  DARWIN_VA_BEGIN(format);
  int r = vsnprintf(out, n, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_asprintf(char **out, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vasprintf(out, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_sscanf(const char *in, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vsscanf(in, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_fscanf(FILE *f, const char *format, ...) {
  DARWIN_VA_BEGIN(format);
  int r = vfscanf(f, format, l);
  va_end(l);
  return r;
}

int mcfm_darwin_vfprintf(FILE *f, const char *format, char *darwin_ap) {
  va_list l;
  from_darwin(&l, darwin_ap);
  return vfprintf(f, format, l);
}

int mcfm_darwin_vsnprintf(char *out, size_t n, const char *format, char *darwin_ap) {
  va_list l;
  from_darwin(&l, darwin_ap);
  return vsnprintf(out, n, format, l);
}

// open/fcntl/ioctl: the variadic argument is read here, the numbers are translated in files.cpp.
int mcfm_darwin_open_needs_mode(int darwin_flags);
int mcfm_darwin_open_impl(const char *path, int darwin_flags, int mode);
int mcfm_darwin_fcntl_impl(int fd, int darwin_cmd, intptr_t arg);
int mcfm_darwin_ioctl_impl(int fd, unsigned long darwin_request, intptr_t arg);

int mcfm_darwin_open(const char *path, int darwin_flags, ...) {
  int mode = 0;
  if (mcfm_darwin_open_needs_mode(darwin_flags)) {
    DARWIN_VA_BEGIN(darwin_flags);
    mode = va_arg(l, int);
    va_end(l);
  }
  return mcfm_darwin_open_impl(path, darwin_flags, mode);
}

// The third argument's slot is read whatever the command: it lies in the caller's frame, and
// commands without an argument ignore it.
int mcfm_darwin_fcntl(int fd, int darwin_cmd, ...) {
  DARWIN_VA_BEGIN(darwin_cmd);
  intptr_t arg = va_arg(l, intptr_t);
  va_end(l);
  return mcfm_darwin_fcntl_impl(fd, darwin_cmd, arg);
}

int mcfm_darwin_ioctl(int fd, unsigned long darwin_request, ...) {
  DARWIN_VA_BEGIN(darwin_request);
  intptr_t arg = va_arg(l, intptr_t);
  va_end(l);
  return mcfm_darwin_ioctl_impl(fd, darwin_request, arg);
}
