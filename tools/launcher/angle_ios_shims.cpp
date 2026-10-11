// What ANGLE's static iOS libraries (Godot's build, tools/launcher/fetch_angle_ios.sh) expect
// from the program that links them; Godot supplies these itself.
#include <pthread.h>
#include <time.h>

// angle::GetCurrentSystemTime (the overlay's clock) and SetCurrentThreadName (worker threads).
namespace angle {
double GetCurrentSystemTime() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
}
void SetCurrentThreadName(const char *name) { pthread_setname_np(name); }
}  // namespace angle

// astcenc, the ASTC software decoder, which Godot links separately. ANGLE uses it only for ASTC textures
// on GPUs that cannot sample ASTC; every iOS GPU since the A8 can (and the game ships none), so
// here it is reported unavailable: ANGLE then never decodes on the CPU.
enum astcenc_profile {};
enum astcenc_error { ASTCENC_SUCCESS = 0, ASTCENC_ERR_UNSUPPORTED = 1 };
struct astcenc_config;
struct astcenc_context;
struct astcenc_image;
struct astcenc_swizzle;

astcenc_error astcenc_config_init(astcenc_profile, unsigned, unsigned, unsigned, float, unsigned, astcenc_config *) {
  return ASTCENC_ERR_UNSUPPORTED;
}
astcenc_error astcenc_context_alloc(const astcenc_config *, unsigned, astcenc_context **context) {
  *context = nullptr;
  return ASTCENC_ERR_UNSUPPORTED;
}
void astcenc_context_free(astcenc_context *) {}
astcenc_error astcenc_decompress_image(astcenc_context *, const unsigned char *, unsigned long, astcenc_image *,
                                       const astcenc_swizzle *, unsigned) {
  return ASTCENC_ERR_UNSUPPORTED;
}
astcenc_error astcenc_decompress_reset(astcenc_context *) { return ASTCENC_ERR_UNSUPPORTED; }
const char *astcenc_get_error_string(astcenc_error) { return "ASTC decoding is not available"; }
