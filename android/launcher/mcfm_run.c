// mcfm-run: loads libmcfm_launcher.so (found through LD_LIBRARY_PATH: bionic does not search the
// executable's directory; tools/android/adb_run.sh sets it) and runs
// mcfm_run_main (run.cpp). A plain C program: the launcher library and its C++ runtime stay out of
// the process's global symbol namespace, as they will be when an app loads them.
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
  void *launcher = dlopen("libmcfm_launcher.so", RTLD_NOW | RTLD_LOCAL);
  int (*run)(int, char **) = launcher ? (int (*)(int, char **))dlsym(launcher, "mcfm_run_main") : NULL;
  if (!run) {
    fprintf(stderr, "mcfm-run: %s\n", dlerror());
    return 2;
  }
  return run(argc, argv);
}
