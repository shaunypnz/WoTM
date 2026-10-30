# Vulkan renderer

Goal: the game draws through Vulkan on Linux, with OpenGL gone once it matches. This is what sets this fork apart.

## Decision: replace rlgl, not the drawing code
All of `native/` draws through raylib, and raylib draws only through `rlgl`. So the seam is the `rlgl.h` API: `native/rlvk/rlvk.cpp` implements it on [wgpu-native](https://github.com/gfx-rs/wgpu-native) restricted to Vulkan. About 45,000 lines of native code, and raylib's text, shapes and textures, keep working unchanged.

Why wgpu rather than raw Vulkan: synchronization and memory are handled, the earlier engine in `legacy/ps2-web-recomp` already ships it with a pinned checksum, and its picture filters (`runtime/src/gs/gpu/post.wgsl`, `blit.wgsl`, MIT) can be reused.

Images stay in OpenGL row order (row 0 at the bottom). The vertex shader flips Y and converts Z, so the render textures, UVs and negative-height `src` rectangles in `native/` keep their meaning.

## Inventory (what the port uses today)
| Area | Use | Where |
|---|---|---|
| rlgl immediate mode and state | `rlDrawRenderBatchActive` 46, `rlSetTexture` 35, `rlTexCoord2f` 32, `rlVertex3f` 29, `rlBegin` 24, `rlTextureParameters` 22, blend, depth and cull state | `wotm_fx.hpp`, `wotm_native.hpp`, `runtime/wotm_padsel.inc`, `runtime/wotm_live.inc` |
| raylib on rlgl | `DrawTexturePro`, shapes, text, `BeginTextureMode`, `LoadRenderTexture`, `GenTextureMipmaps`, `LoadImageFromScreen` | everywhere; needs no changes |
| Custom shaders | GLSL 330: alpha shader, text shader, model shader, FXAA and sharpen | `wotm_native.hpp`, `wotm_model.hpp`, `wotm_post.hpp` |
| Direct GL | framebuffers, MSAA and blits | `wotm_msaa.hpp` |
| Direct GL | own VBO/VAO draws, `glDrawArrays`, uniforms | `wotm_fx.hpp`, `wotm_native.hpp` |
| Direct GL | depth readback, PBOs, fences and queries for lens flares | `wotm::AsyncDepth` in `wotm_fx.hpp`, `glReadPixels` |
| Window | GLFW with a GL context, inside raylib | runtime patch |

## Done
- `native/rlvk/rlvk.cpp`: batches, RGBA8 textures, framebuffers with depth, blending, depth test, culling, matrices and readback.
- `tools/linux/test_rlvk.sh`: draws without a window and checks the pixels on a real Vulkan GPU. Passes on an RX 570 (RADV). Its expected values are reasoned from how OpenGL behaves, and haven't yet been compared with the real rlgl.

## To do, in order
1. Complete the rlgl API the port uses, and run the same test on the real rlgl to prove the expected values.
2. Build raylib on rlvk with no GL context (`GLFW_NO_API`), and present the backbuffer to a window surface.
3. Port the direct GL uses and shaders above, to WGSL.
4. A fight on Vulkan through the gate, compared with the OpenGL build, then FXAA, sharpening and render scale. OpenGL stays the default until the owner has played it.
