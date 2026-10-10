// conformance_host <darwin_conformance.dylib>: the reference run of the conformance fixture on
// the Mac (real Darwin libSystem and libc++), for tools/tests/android_launcher_test.sh.
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  void *h = dlopen(argv[1], RTLD_NOW);
  int (*run)(int) = h ? (int (*)(int))dlsym(h, "conformance_main") : NULL;
  if (!run) { fprintf(stderr, "conformance_host: %s\n", dlerror()); return 1; }
  return run(0);
}
