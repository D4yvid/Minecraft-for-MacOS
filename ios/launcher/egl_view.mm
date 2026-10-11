// The iOS app's drawable (docs/LAUNCHER.md, Stage 4): ANGLE (OpenGL ES 3 on Metal) with an EGL
// window surface on the view's CAMetalLayer, as the macOS launcher does. The surface is
// framebuffer 0, which the engine binds for the screen. Every GL call of the app goes through
// here, to ANGLE (the app does not link the system's OpenGLES).
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <cstdio>

#include "egl_min.h"
#include "mcfm_ios.h"

extern "C" {
EGLDisplay eglGetPlatformDisplay(EGLenum, void *, const EGLAttrib *);
EGLBoolean eglInitialize(EGLDisplay, EGLint *, EGLint *);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void *, const EGLint *);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint *);
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
EGLBoolean eglDestroyContext(EGLDisplay, EGLContext);
EGLBoolean eglTerminate(EGLDisplay);
EGLint eglGetError(void);
void glBindFramebuffer(unsigned, unsigned);
void glViewport(int, int, int, int);
void glReadPixels(int, int, int, int, unsigned, unsigned, void *);
void glPixelStorei(unsigned, int);
void glBindBuffer(unsigned, unsigned);
unsigned glGetError(void);
void glFinish(void);
}

namespace {
constexpr EGLint kEGL_SURFACE_TYPE = 0x3033, kEGL_WINDOW_BIT = 0x0004, kEGL_WIDTH = 0x3057, kEGL_HEIGHT = 0x3056;
EGLDisplay g_display = nullptr;
EGLContext g_context = nullptr;
EGLSurface g_surface = nullptr;
bool g_tried = false;

// A failed init leaves nothing behind (and is not tried again: the app shows why it cannot run).
int fail(const char *what) {
  std::fprintf(stderr, "mcfm: ANGLE: %s (0x%x)\n", what, eglGetError());
  if (g_display) {
    eglMakeCurrent(g_display, nullptr, nullptr, nullptr);
    if (g_surface) eglDestroySurface(g_display, g_surface);
    if (g_context) eglDestroyContext(g_display, g_context);
    eglTerminate(g_display);
  }
  g_display = g_context = g_surface = nullptr;
  return 0;
}
}  // namespace

extern "C" {

int mcfm_ios_egl_init(void *metal_layer) {
  if (g_tried) return g_surface != nullptr;
  g_tried = true;
  const EGLAttrib display_attribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
  g_display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, display_attribs);
  EGLint major = 0, minor = 0;
  if (!g_display) return fail("no EGL display");
  if (!eglInitialize(g_display, &major, &minor)) return fail("EGL does not initialize");
  const EGLint config_attribs[] = {kEGL_SURFACE_TYPE, kEGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(g_display, config_attribs, &config, 1, &count) || count < 1) return fail("no EGL config");
  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  g_context = eglCreateContext(g_display, config, nullptr, context_attribs);
  g_surface = eglCreateWindowSurface(g_display, config, metal_layer, nullptr);
  if (!g_context || !g_surface || !eglMakeCurrent(g_display, g_surface, g_surface, g_context))
    return fail("cannot draw into the view");
  std::fprintf(stderr, "mcfm: EGL %d.%d (ANGLE, Metal)\n", major, minor);
  return 1;
}

void mcfm_ios_egl_size(int *w_px, int *h_px) {
  EGLint w = 0, h = 0;
  if (g_surface) {
    eglQuerySurface(g_display, g_surface, kEGL_WIDTH, &w);
    eglQuerySurface(g_display, g_surface, kEGL_HEIGHT, &h);
  }
  *w_px = w;
  *h_px = h;
}

void mcfm_ios_egl_begin_frame(void) {
  if (!g_surface) return;
  int w = 0, h = 0;
  eglMakeCurrent(g_display, g_surface, g_surface, g_context);
  mcfm_ios_egl_size(&w, &h);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w, h);
}

unsigned mcfm_ios_egl_end_frame(void) {
  if (!g_surface) return 0;
  unsigned error = glGetError();
  eglSwapBuffers(g_display, g_surface);
  return error;
}

void mcfm_ios_egl_finish(void) {
  if (g_context) glFinish();
}

void mcfm_ios_egl_read_pixels(int w, int h, void *rgba) {
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

}  // extern "C"
