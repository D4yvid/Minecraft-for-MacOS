# Renderer: what we know (Minecraft PE 0.15.10)

Status: **research notes, nothing implemented.** Facts are marked ✅ verified (with how) or
❓ assumed / to verify.

## Frame loop on iOS / macOS

- ✅ `minecraftpeViewController` drives frames from a `CADisplayLink` (`startAnimation`,
  `drawFrame`). `drawFrame` (iOS `0x10070F670`): `[view setFramebuffer]`, drains
  `mMainQueue` (std::function jobs), then calls `MinecraftClient::update()` (App vtable slot 19,
  see [minecraftclient-vtable.md](minecraftclient-vtable.md)). (IDA decompilation.)
- ✅ `EAGLView` (UIView, `layerClass` = `CAEAGLLayer`) owns the GL objects:
  `createFramebuffer` (colour renderbuffer from the layer + 24/8 depth-stencil renderbuffer),
  `setFramebuffer` (binds, `glViewport` to the framebuffer size, recreates it if deleted),
  `presentFramebuffer`, `layoutSubviews` (deletes the framebuffer → recreated next frame). Log
  line `Created an OpenGL ES 3 context` at startup. (IDA + runtime log.)
- ✅ The engine learns its size only through App slots 21 `setRenderingSize(w, h)` and 20
  `setUISizeAndScale(w, h, scale)`; the macOS mod calls them on every resize
  (`macos/src/resize.mm`).
- ✅ Swapping happens in the iOS glue (`presentFramebuffer`), not in the engine.

## Engine render architecture (from Android symbols)

- ✅ A `mce::` ("Minecraft Engine") abstraction: `RenderDevice`, `RenderContext`, `Texture`,
  `Buffer`, `Shader`, `FrameBufferObject`, `RenderMaterialGroup`, `ShaderGroup`,
  `RenderStage`, constant buffers. 694 `mce::` symbols. (`nm -D libminecraftpe.so`.)
- ✅ Backend is **OpenGL**, selected at compile time: `RenderContextOGL`, `TextureOGL`,
  `ShaderOGL`, `BufferOGL`, `FrameBufferObjectOGL`… derive from `*Base` classes and have **no
  vtables** (only `RenderStage*`, `RenderMaterialGroup`, `ShaderGroup`, `ShaderConstant*`,
  `QuadIndexBuffer` do). So a new backend cannot be dropped in by vtable patching.
- ✅ Game-level renderers sit above `mce`: `LevelRenderer` (115 symbols), `BlockTessellator`,
  `GameRenderer`, `ScreenRenderer`, `MinecraftUIRenderContext`, `EntityRenderDispatcher`,
  `ItemInHandRenderer`, `HolosceneRenderer`/`HolographicPostRenderer` (VR), …
- ✅ iOS binary imports **91 `gl*` functions** from `OpenGLES.framework` plus `EAGLContext`,
  `CAEAGLLayer`; Android imports `gl*` + `egl*` (`eglSwapBuffers`, `eglCreateContext`, …).
- ✅ Shaders ship as source in the game data: `data/shaders/*.vertex` / `*.fragment` plus
  `uniforms.json`; materials in `data/materials/*.material` / `*.json`. (Listed in the iOS app.)

## Options for "our own renderer"

| Approach | How | Pros | Cons |
|---|---|---|---|
| **A. Replace the GL layer** | Rebind the main image's 91 imported `gl*` symbols (fishhook-style rebinding of lazy/non-lazy symbol pointers — this old binary uses classic dyld binds) to our implementation, plus replace `EAGLView`'s context/present with our own layer | Engine untouched; one seam; same idea works on Android (rebind `gl*`/`egl*` imports in `libminecraftpe.so` GOT) | Must implement GLES 3 semantics faithfully (or embed ANGLE's Metal backend, ~large); shaders still GLSL |
| **B. Hook the `mce::*OGL` functions** | Inline-hook the ~39 `RenderContextOGL`/`TextureOGL`/`ShaderOGL`/`BufferOGL`/`RenderDevice` methods and route to a Metal/Vulkan backend | Higher-level API (draw calls, textures, buffers) → cleaner Metal mapping | Needs an inline hooking library on Apple (none in the repo yet; Android had Substrate in the old injection project); iOS is stripped → each method must be located in IDA (Android names help); non-virtual calls may be inlined in places |
| **C. Wrap, don't replace** | Keep GL, add post-processing / resolution scaling / FPS limiting around `drawFrame` / `presentFramebuffer` | Small, safe, immediate wins (e.g. render at lower resolution, MSAA, vsync control) | Not a new renderer |

Suggested path: start with **C** (instrument the frame: timing, GL call counts, a debug
overlay) to learn the frame structure, then decide between **A** (most portable; ANGLE/Metal is
a known-good GLES implementation on macOS/iOS) and **B**.

## Open questions

- ❓ Exact GLES version features used (instancing? MRT? UBOs?) — log the 91 imports' call counts.
- ❓ Whether the engine calls GL from threads other than the main thread.
- ❓ iOS addresses of the `mce::*OGL` methods (map via Android names + IDA; the strings in
  `RenderContextOGL` error paths are good anchors).
- ❓ How `HolosceneRenderer` (VR) affects the frame graph when disabled.
