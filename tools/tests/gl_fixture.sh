#!/bin/bash
# usage: gl_fixture.sh <outdir> [missing]
# A converted image calling GLES through the OpenGLES framework, as the game does (Stage 3b):
# <outdir>/libminecraftpe.dylib and <outdir>/imports.tsv. A stand-in OpenGLES.framework (built
# here) provides the names on the Mac; on Android they must resolve to the system's GLES.
# "missing": the image also calls glMcfmMissingOES, which no GLES has (the load must fail).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"; mkdir -p "$OUT"
MISSING=0; [ "${2:-}" = missing ] && MISSING=1
CC=(clang -arch arm64 -mmacosx-version-min=11.0)
cat > "$OUT/opengles.c" <<'C'
void glClearColor(float r, float g, float b, float a) {}
void glClear(unsigned mask) {}
void glReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void *p) {}
void glGenVertexArraysOES(int n, unsigned *arrays) {}
void glBindRenderbufferOES(unsigned target, unsigned rb) {}
unsigned glGetError(void) { return 0; }
void glMcfmMissingOES(void) {}
const char *kEAGLColorFormatRGBA8 = "EAGLColorFormatRGBA8";
C
"${CC[@]}" -dynamiclib "$OUT/opengles.c" -install_name /System/Library/Frameworks/OpenGLES.framework/OpenGLES -o "$OUT/OpenGLES"
cat > "$OUT/gl.c" <<'C'
void glClearColor(float r, float g, float b, float a);
void glClear(unsigned mask);
void glReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void *p);
void glGenVertexArraysOES(int n, unsigned *arrays);
void glBindRenderbufferOES(unsigned target, unsigned rb);
unsigned glGetError(void);
void glMcfmMissingOES(void);
extern const char *kEAGLColorFormatRGBA8;
// The clear color read back, packed RGB; the OES entry points must resolve too.
__attribute__((used)) int gl_check(int unused) {
  (void)unused;
  unsigned vao = 0;
  glGenVertexArraysOES(1, &vao);
  glBindRenderbufferOES(0x8D41, 0);
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  glClear(0x4000);
  unsigned char p[4] = {0};
  glReadPixels(0, 0, 1, 1, 0x1908, 0x1401, p);
  return glGetError() == 0 ? (p[0] << 16 | p[1] << 8 | p[2]) : -1;
}
__attribute__((used)) const char *eagl_constant(void) { return kEAGLColorFormatRGBA8; }
#if MCFM_MISSING
__attribute__((used)) void call_missing(void) { glMcfmMissingOES(); }
#endif
int main(void) { return gl_check(0); }
C
"${CC[@]}" -O1 -DMCFM_MISSING=$MISSING -Wl,-no_fixup_chains -Wl,-headerpad,0x1000 "$OUT/gl.c" "$OUT/OpenGLES" -o "$OUT/gl.mac"
vtool -set-build-version ios 15.0 15.0 -replace -output "$OUT/gl" "$OUT/gl.mac"
rm -f "$OUT/gl.mac"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" imports "$OUT/gl" > "$OUT/imports.tsv"
python3 -I "$ROOT/tools/launcher/mcfm_image.py" dylib "$OUT/gl" "$OUT/libminecraftpe.dylib"
codesign -f -s - "$OUT/libminecraftpe.dylib" 2>/dev/null
