#pragma once
// A Module is one feature (Win10 UI, keyboard+mouse, ...). A Client owns the platform
// and initialises its modules in registration order. Modules are not owned. C++11.
#include <mcfm/platform.h>

#include <vector>

namespace mcfm {

class Module {
 public:
  virtual ~Module() {}
  virtual const char *name() const = 0;
  virtual bool init(Platform &platform) = 0;
};

class Client {
 public:
  explicit Client(Platform &platform) : platform_(platform) {}

  // false for null or a name already registered.
  bool add(Module *module);

  // Initialises every module (a failure does not stop the others); true if all succeeded.
  bool init();

  Platform &platform() { return platform_; }

 private:
  Platform &platform_;
  std::vector<Module *> modules_;
};

}  // namespace mcfm
