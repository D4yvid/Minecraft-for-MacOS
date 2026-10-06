#include <mcfm/module.h>
#include "fake_platform.h"

#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<std::string> order;

class TestModule : public mcfm::Module {
 public:
  TestModule(const char *n, bool ok) : name_(n), ok_(ok) {}
  const char *name() const { return name_; }
  bool init(mcfm::Platform &) { order.push_back(name_); return ok_; }
 private:
  const char *name_;
  bool ok_;
};

int main() {
  // modules init in registration order; all succeed -> true
  {
    order.clear();
    FakePlatform p;
    mcfm::Client c(p);
    TestModule a("a", true), b("b", true);
    EXPECT(c.add(&a));
    EXPECT(c.add(&b));
    EXPECT(c.init());
    EXPECT(order.size() == 2 && order[0] == "a" && order[1] == "b");
    EXPECT(p.logged("a: ok") && p.logged("b: ok"));
  }
  // one failing module: init() is false, but every module still gets its chance
  {
    order.clear();
    FakePlatform p;
    mcfm::Client c(p);
    TestModule a("a", false), b("b", true);
    c.add(&a);
    c.add(&b);
    EXPECT(!c.init());
    EXPECT(order.size() == 2);
    EXPECT(p.logged("a: FAILED"));
    EXPECT(p.logged("b: ok"));
  }
  // duplicate names and null modules are rejected
  {
    FakePlatform p;
    mcfm::Client c(p);
    TestModule a("same", true), b("same", true);
    EXPECT(c.add(&a));
    EXPECT(!c.add(&b));
    EXPECT(!c.add(0));
  }
  if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
  std::printf("client_test: all passed\n");
  return 0;
}
