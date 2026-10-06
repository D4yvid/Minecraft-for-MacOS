#include <mcfm/module.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mcfm {

void logf(Platform &p, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  p.log(buf);
}

bool Client::add(Module *module) {
  if (!module) return false;
  for (size_t i = 0; i < modules_.size(); i++)
    if (std::strcmp(modules_[i]->name(), module->name()) == 0) return false;
  modules_.push_back(module);
  return true;
}

bool Client::init() {
  bool all = true;
  for (size_t i = 0; i < modules_.size(); i++) {
    bool ok = modules_[i]->init(platform_);
    logf(platform_, "module %s: %s", modules_[i]->name(), ok ? "ok" : "FAILED");
    all = all && ok;
  }
  return all;
}

}  // namespace mcfm
