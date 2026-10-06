#include <mcfm/platform.h>

#include <cstdarg>
#include <cstdio>

namespace mcfm {

void logf(Platform &p, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  p.log(buf);
}

}  // namespace mcfm
