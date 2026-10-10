#pragma once
// The few EGL / GLES entry points mcfm-launch uses, loaded from ANGLE with dlsym so the
// launcher builds (and make test runs) without ANGLE. Values from the Khronos registry.
#include <cstdint>

typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;
typedef intptr_t EGLAttrib;

enum : EGLint {
  EGL_NONE = 0x3038, EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022,
  EGL_ALPHA_SIZE = 0x3021, EGL_DEPTH_SIZE = 0x3025, EGL_STENCIL_SIZE = 0x3026,
  EGL_RENDERABLE_TYPE = 0x3040, EGL_OPENGL_ES3_BIT = 0x0040, EGL_CONTEXT_CLIENT_VERSION = 0x3098,
  EGL_PLATFORM_ANGLE_ANGLE = 0x3202, EGL_PLATFORM_ANGLE_TYPE_ANGLE = 0x3203,
  EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE = 0x3489,
};
constexpr unsigned GL_FRAMEBUFFER = 0x8D40;
constexpr unsigned GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401;
constexpr unsigned GL_PACK_ALIGNMENT = 0x0D05, GL_PACK_ROW_LENGTH = 0x0D02, GL_PIXEL_PACK_BUFFER = 0x88EB;

struct Egl {
  EGLDisplay (*GetPlatformDisplay)(EGLenum, void *, const EGLAttrib *);
  EGLBoolean (*Initialize)(EGLDisplay, EGLint *, EGLint *);
  EGLBoolean (*ChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
  EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
  EGLSurface (*CreateWindowSurface)(EGLDisplay, EGLConfig, void *, const EGLint *);
  EGLBoolean (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
  EGLBoolean (*SwapBuffers)(EGLDisplay, EGLSurface);
  EGLint (*GetError)(void);
  void (*BindFramebuffer)(unsigned, unsigned);
  void (*Viewport)(int, int, int, int);
  void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
  void (*PixelStorei)(unsigned, int);
  void (*BindBuffer)(unsigned, unsigned);
};
