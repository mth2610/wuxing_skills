# RLVK Vulkan 1.1 Backend — Handoff Document

> **Purpose**: complete context for an AI (or human) continuing this work with zero prior
> conversation. Read this top to bottom before touching `third_party/vulkan/rlvk.h`.
> Written 2026-07-15, after the 1.3→1.1 retarget was completed and compile-verified.

---

## 1. Vision & why this exists

The user's goal (their words, translated): **many machines are stuck on Vulkan 1.1; if we
solve this, the engine has a real future.** Not just particles — beautiful VFX must run on
old/weak devices. Long-term: extract a standalone renderer/engine, not wuxing-game-only.

Tiering decided with the user:
- **Old desktop + macOS + (for now) Android → existing OpenGL/GLES backend** (unchanged).
- **Vulkan 1.1+ devices → rlvk**, one single code path from weakest 1.1 Android driver to
  newest desktop GPU. 1.3 features are *optional accelerators only* where trivially gated.
- Key motivator: **compute particles on Android**. Mali GLES silently fails vertex-stage
  SSBO reads (see `CORE_ISSUES.md` Item 5 — compute path permanently disabled on Android).
  Vulkan *mandates* vertex-stage storage-buffer reads on every conformant device, so the
  existing `compute/gpu_particle_system` architecture (compute writes SSBO → vertex shader
  builds billboards from SSBO) works on Vulkan/Mali where GLES could not.

## 2. What rlvk is

`third_party/vulkan/rlvk.h` (~7,600 lines, single header, zlib license, v1.1) implements
**the complete rlgl API on Vulkan** — same `rl*` functions, different rasterizer. rlgl.h is
included verbatim and never modified. The game keeps calling raylib/rlgl normally.

Integration model (documented in the file header): in the ONE translation unit that would
define `RLGL_IMPLEMENTATION`, define `RLVK_IMPLEMENTATION` and include `rlvk.h` instead.
`GRAPHICS_API_VULKAN_14` is defined for backend detection; `GRAPHICS_API_OPENGL_33` is
defined ONLY to fix rlgl.h's `rlVertexBuffer` struct layout. Never combine both
implementations in one build (`#error`-guarded).

Platform hooks (NOT part of rlgl's API): platform layer creates `VkSurfaceKHR` →
`rlvkAttachSurface(surface)`; `SwapScreenBuffer()` → `rlvkPresent()`;
`rlvkSetMsaaSamples(4)` before attach for `FLAG_MSAA_4X_HINT`; `rlvkGetInstance()` for
`glfwCreateWindowSurface`.

## 3. State: retarget COMPLETE — headless paths **RUNTIME-VERIFIED on MoltenVK**

The original rlvk targeted Vulkan 1.3 + 6 required extensions. It has been fully retargeted:
**the only hard device requirement is now core Vulkan 1.1 + `VK_KHR_swapchain`.**

**Runtime verification (2026-07-15)**: LunarG Vulkan SDK 1.3.296 installed at
`~/VulkanSDK/1.3.296.0` (user-level). MoltenVK 1.2.11 on Intel Iris 6000 reports device API
**1.2** → sync2 absent → the sync1 shim and one-shot staging paths run for real, not the
native fast paths. `./scripts/run_rlvk_runtime_test.sh` builds + runs
`third_party/vulkan/tests/rlvk_runtime_test.c` with validation layers: **20/20 PASS, zero
validation errors** — device init, texture staging roundtrips (RGBA8 + partial update +
RGB→RGBA expand/repack), SSBO roundtrips (upload/read/partial/GPU-copy), compute dispatch
with loose uniforms (one-shot path), runtime GLSL 330 graphics compile through shaderc with
the clip-z epilogue, clean shutdown. Windowed/draw paths (render-pass cache, batch,
present) still need a surface — untested until the platform layer (§4.2) exists.

Runtime bugs found & fixed during bring-up:
- `rlvkDeferDestroy`'s immediate-destroy branch (frameCounter==0) leaked pipelines.
- Descriptor-write scratch arrays in dispatch were undersized for the full binding mix.
- **MoltenVK/Intel driver bug (bisected via a raw-Vulkan repro, no rlvk involved): merely
  DECLARING storage-image bindings in a compute descriptor-set layout makes a UBO at a
  later binding read as zeros**, even when the images are never written or statically
  used. Fix: the fixed compute layout has NO storage-image bindings (0–7 SSBO, 12–13
  sampler, 14 UBO); when image load/store is needed, give images their own descriptor SET.
- **shaderc's `auto_bind_uniforms` rebases even explicitly-bound UBOs** (`layout(binding=N)
  uniform` becomes N + base). Compute shaders must use loose uniforms (the GL-style path)
  or explicit-std430 SSBOs (storage-buffer kind has no base set, bindings preserved).

### 3.1 The conversion table (what replaced what)

| Old 1.3 requirement | Replacement (single path unless noted) | Where |
|---|---|---|
| `dynamicRendering` | Cached `VkRenderPass` + `VkFramebuffer` (two bounded tables in `rlvkData`: `renderPasses[32]`, `framebuffers[64]`, keyed by `rlvkRenderPassKey` / pass+views+extent). All scope opens go through `rlvkBeginScopeRenderPass()`; all closes are plain `vkCmdEndRenderPass`. Attachment layouts are initial==final (COLOR/DEPTH_ATTACHMENT_OPTIMAL) so the pass never fights rlvk's manual layout tracking; no subpass dependencies (explicit barriers outside, as before). MSAA resolve = subpass `pResolveAttachments` (attachment order convention: colors, resolve, depth — everywhere). | `rlvkGetRenderPass`, `rlvkGetFramebuffer`, `rlvkBeginScopeRenderPass`, `rlvkEvictFramebuffersForView` |
| `synchronization2` | **Shim installed into the `vk.` dispatch table** when sync2 absent: `rlvkCmdPipelineBarrier2Compat` / `rlvkQueueSubmit2Compat` translate `VkDependencyInfo`/`VkSubmitInfo2` → core sync1 calls. All ~14 call sites keep their sync2 shape untouched; 1.3 devices keep native entry points. Bit mapping: COPY/BLIT/RESOLVE/CLEAR→TRANSFER, INDEX/VERTEX_ATTRIBUTE_INPUT→VERTEX_INPUT, SAMPLED/STORAGE_READ→SHADER_READ, NONE→TOP/BOTTOM_OF_PIPE. | shim section right after the `vk` struct; installed in `rlvkLoadEntrypoints` |
| `VK_EXT_host_image_copy` | **Removed entirely.** Classic staging buffer + one-shot submission. `rlvkOneShotBegin/End` create a TRANSIENT command pool per call so an in-progress frame recording is never clobbered. Helpers: `rlvkStagingUploadImage` (layers, x/y offset), `rlvkStagingReadImage`, `rlvkCmdTransitionImage` (mip+layer ranges), `rlvkHostTransitionImage`. Converted: rlLoadTexture, cubemap, rlUpdateTexture load-time branch, rlGenTextureMipmaps (whole chain packed into ONE staging buffer, one submission), rlReadTexturePixels. | staging section before `rlvkCreateVBO` |
| `VK_KHR_push_descriptor` | Optional. Same shim pattern: `rlvkPushDescriptorSetCompat` installed into `vk.CmdPushDescriptorSetKHR` updates a CPU shadow (`pushedView/pushedSampler` reused + `shadowUbo[2]`) and sets `set0Dirty`. `rlvkFlushSet0(cmdBuffer)` — called before **all 5 draw sites** (batch draw/drawIndexed, mesh draw/drawIndexed, quad draw) — allocates a snapshot set from `descPools[frame]` (`RLVK_DESC_SETS_PER_FRAME` 1024), writes all 16 samplers (NULL→default texture) + up-to-2 UBOs, binds it. Pools reset at both cb-reset points (alongside the `memset(pushedView)` lines). Native push descriptors used when available. | `rlvkPushDescriptorSetCompat`, `rlvkFlushSet0` |
| `VK_EXT_depth_clip_control` | **Removed entirely — deliberately, on every device.** GL [-1,1] clip-z is remapped by a vertex-shader epilogue `gl_Position.z = (gl_Position.z + gl_Position.w)*0.5` in TWO places that must stay in sync: (a) injected by `rlvkInjectClipZEpilogue` in `rlvkCompileGlsl` for every runtime-compiled VS (whole-word-renames `main`→`rlvk_main_`, appends wrapper `main`), (b) baked into the embedded default shader source `third_party/vulkan/shaders/rlvk_default.vert`. **Trap: re-enabling the extension without removing the epilogue double-transforms depth.** | `rlvkInjectClipZEpilogue` |
| `VK_EXT_vertex_attribute_divisor` (zero-divisor broadcasts) | **Removed entirely.** Missing-attribute broadcasts now use `stride = 0, inputRate = VERTEX` bindings — core Vulkan 1.0 semantics (address = offset + index*0). Real instancing (`RLVK_VLAYOUT_MESH_INSTANCED`) is plain INSTANCE rate (divisor-1 equivalent, core). Known caveat: **MoltenVK portability subset rejects stride < format size** — only matters if macOS-over-Vulkan ever becomes a target (it is not; macOS uses GL). | `rlvkBuildVertexInput`, `rlvkAppendDummyAttribs` |
| `bresenhamLines`, `wideLines`, `fillModeNonSolid` | Optional Caps. Line state chained into pipelines only when ext present (default line raster otherwise — cosmetic delta, accepted). `wideLines` never requested (all pipelines use lineWidth 1.0; `rlSetLineWidth` only feeds `rlGetLineWidth`). Wire/point polygon modes degrade to FILL when `fillModeNonSolid` absent (pipeline-key time). | pipeline creation; `key.polygonMode` computation |
| `vkCmdSetViewportWithCount` / `ScissorWithCount` / `vkCmdWriteTimestamp2` (1.3-only, missed by the first audit) | Plain `vkCmdSetViewport/vkCmdSetScissor(first=0,count=1)` + pipeline `viewportCount=scissorCount=1` + `VK_DYNAMIC_STATE_VIEWPORT/SCISSOR`; `vkCmdWriteTimestamp` with TOP/BOTTOM_OF_PIPE stages (GPU-trace debug path). | pipeline dynamic state; `RLVK_GPU_TRACE` sites |
| shaderc target env vulkan **1.3** | **vulkan 1.1 / SPIR-V 1.3** — the old target emitted SPIR-V 1.6, *invalid on 1.1 drivers*. This was load-bearing, not cosmetic. | `rlvkCompileGlsl` |

### 3.2 New capabilities added (beyond the retarget)

**Swapchain recreation** (was `TODO(vk)`, mandatory for Android rotate/pause/resume, fixes
desktop resize): `rlvkDestroySwapchainSizedObjects()` (destroys swapchain, its views +
per-image present semaphores, per-frame depth/inter/msaa targets, evicting cached
framebuffers per dying view) + `rlvkRecreateSwapchain()` (device-idle drain → destroy →
re-run `rlvkAttachSurface`, which is now re-entrant: acquire semaphores + frame fences are
guarded once-only). Wired at: acquire `OUT_OF_DATE` (recreate + one retry; failed acquire
leaves the semaphore unsignaled so reuse is safe; still-0x0 = minimized → skip frame) and
present `OUT_OF_DATE|SUBOPTIMAL` (recreate after frame bookkeeping).

**Compute — fully implemented** (was stubs; this unblocks the Android particle goal):
- Fixed compute set-0 layout, created lazily (`rlvkInitComputeLayout`): bindings **0–7
  STORAGE_BUFFER, 8–11 STORAGE_IMAGE, 12–13 COMBINED_IMAGE_SAMPLER, 14 UNIFORM_BUFFER**
  (the implicit loose-uniform block). shaderc compute-stage auto-binding bases match
  (image 8, texture/sampler 12, buffer 14); SSBOs declare explicit `std430, binding=0..7`
  in GLSL themselves.
- Flow: `rlLoadShader(code, RL_COMPUTE_SHADER)` stashes a copy of the GLSL in the slot
  (`pendingCode`) → `rlLoadShaderProgramCompute(csId)` lazily loads shaderc, compiles
  (stage 2), reflects the loose-uniform block + samplers via `rlvkReflectSpv` (offsets ride
  the `vsStage/vsBlockSize` fields), creates module + monolithic compute pipeline against
  `computePipelineLayout`, returns csId (stage slot becomes program slot).
- `rlBindShaderBuffer(id, index)` / `rlBindImageTexture(id, unit, ...)` record into
  `RLVK.computeSSBO[8]` / `computeImage[4]` (GL bind-then-dispatch semantics).
- `rlComputeShaderDispatch(x,y,z)`: suspends the open render scope (same FBO-close +
  `vkCmdEndRenderPass` + resume dance every mid-frame copy uses — dispatch is illegal
  inside a render pass), allocates a snapshot set from `computeDescPools[frame]`
  (`RLVK_COMPUTE_SETS_PER_FRAME` 256, reset with the frame fence), writes SSBOs + storage
  images + sampler uniforms (explicit `rlSetUniformSampler` texture or default) + a
  uniform-block snapshot bump-allocated from the frame arena, binds pipeline+set,
  dispatches, then a broad memory barrier (storage writes → storage/sampled/vertex-attr/
  transfer/uniform reads = `glMemoryBarrier` semantics). Outside a frame (init-time
  seeding) it runs as a one-shot submission instead.
- SSBO functions all real now: `rlLoadShaderBuffer` (DEVICE_LOCAL, STORAGE|TRANSFER_SRC/
  DST|**VERTEX_BUFFER** usage — particle draw reads its SSBO), `rlUpdateShaderBuffer`
  (delegates to `rlvkUploadBuffer`: in-stream arena copy mid-frame, one-shot staging at
  load), `rlReadShaderBuffer` (flush+drain then one-shot readback), `rlCopyShaderBuffer`
  (in-stream or one-shot), `rlUnloadShaderBuffer` (dead-ring deferred + unbinds from
  `computeSSBO`).

**shaderc dlopen for non-Windows** (was Windows-only): tries `libshaderc_shared.so.1/.so`,
`libshaderc.so` (Android NDK naming), `.dylib` variants.

**MoltenVK enumeration support**: `VK_KHR_portability_enumeration` + flag at instance
creation, `VK_KHR_portability_subset` enabled when present (spec requires it).

### 3.3 Files created/changed (all uncommitted in git)

| File | What |
|---|---|
| `third_party/vulkan/rlvk.h` | The retarget (modified, ~7,600 lines). Header docs updated to match. |
| `third_party/vulkan/rlvk_shaders.h` | **Was missing from the repo entirely** (rlvk.h includes it; nothing compiled before). Generated embedded SPIR-V (`rlvkDefaultVertSpv/FragSpv`, uint32 arrays, SPIR-V 1.3). |
| `third_party/vulkan/shaders/rlvk_default.vert/.frag` | Source for the embedded default shader. Interface contract documented in comments: attrib locations 0/1/3, push_constant block == `rlvkPushConstants{mat4 mvp; vec4 colDiffuse}` (80 B), set0 binding0 = texture0, **clip-z epilogue baked in**. |
| `scripts/gen_rlvk_shaders.sh` | Regenerates rlvk_shaders.h via glslc (`--target-env=vulkan1.1`). Auto-finds Android NDK's glslc. |
| `scripts/check_rlvk_compile.sh` | **The verification loop.** Compiles rlvk.h standalone: fetches Vulkan-Headers (git) + raylib 6.0 `raylib.h/config.h/rlgl.h` (curl) into `/tmp/rlvk_check_cache`, builds a TU mimicking rcore.c (`#include "raylib.h"` THEN `#define RLVK_IMPLEMENTATION #include "rlvk.h"`), `-std=c11 -Wall`. Run after every rlvk.h edit. No Vulkan SDK needed (compile only, no link). |
| `third_party/vulkan/include/shaderc/` | Vendored shaderc C headers. `shaderc.h` is from **upstream google/shaderc main** (has `shaderc_compile_options_set_vulkan_rules_relaxed`; the NDK r27/r28 copy is too old and lacks it); `env.h/status.h/visibility.h` from NDK. |
| `RLVK_HANDOFF.md` | This document. |

### 3.4 Dev-environment landmines (this specific Mac, Darwin 21.6 / macOS 12)

- **No Vulkan SDK/runtime installed.** Compile-check works; runtime cannot.
- **NDK glslc crashes** (`__libcpp_verbose_abort` missing in system libc++). Fix that
  worked: copy glslc, `install_name_tool -add_rpath <ndk>/toolchains/llvm/prebuilt/darwin-x86_64/lib`,
  `codesign -f -s -`. (The binary wants `@rpath/libc++.dylib`; no rpath → falls back to the
  ancient system one.) `gen_rlvk_shaders.sh` accepts `GLSLC=<path>` override.
- Vulkan-Headers must be **recent** (v1.3.280 was too old — rlvk uses 1.4-promoted type
  names like `VkVertexInputBindingDivisorDescription`, `VK_IMAGE_USAGE_HOST_TRANSFER_BIT`;
  latest headers work and are compile-time only).
- rlvk.h **must be compiled in raylib's rcore.c context** (raylib.h included first): it uses
  unprefixed `SHADER_LOC_*`, `DEG2RAD`, etc. The existing stub `core/vulkan/wuxing_vulkan.c`
  does NOT do this yet — fix it when wiring the build.

## 4. What remains (ordered; each item is independent unless noted)

### 4.1 Runtime bring-up — headless DONE, windowed/draw paths NEXT
Headless verification is complete on MoltenVK (see §3). What remains needs a window:
1. Platform layer (§4.2), then draw the classic raylib examples through rlvk.
2. Keep validation layers ON (`scripts/run_rlvk_runtime_test.sh` shows the env recipe;
   rlvk never enables layers itself, by design — it only passes a message-id filter).
3. Expected first-bugs: render-pass/framebuffer cache edge cases (MSAA resolve path,
   depth-only shadowmap FBOs, `rlvkFinishSwapchainImage` flip-blit interaction), the
   pool-ring fallback vs native push descriptors (MoltenVK HAS push descriptors, so the
   ring fallback ran headless only via compute — env-force `Caps.pushDescriptor=false` to
   exercise it in draws), stride-0 broadcast bindings on MoltenVK portability (§3.1 caveat
   — mesh draws with missing attributes may need a MoltenVK-specific fallback).
   `RLVK_DEVICE_INDEX/RLVK_DEVICE_NAME` env overrides help device switching.
   Debug helper: `RLVK_DUMP_SPV=<dir>` dumps every shaderc-compiled module for spirv-dis.

### 4.2 Platform layer (blocks 4.1)
- CMake: `WUXING_USE_VULKAN` option exists (`CMakeLists.txt:36`) — it stops compiling
  raylib's `rlgl.c` and compiles `core/vulkan/wuxing_vulkan.c`, but: no Vulkan
  headers/loader on include/link paths, and the TU stub is wrong (must include `raylib.h`
  before `rlvk.h`; see §3.4).
- raylib 6.0 GLFW platform patch (raylib is FetchContent'd into `build/_deps` — patch via
  CMake patch step or vendored platform file): window with `GLFW_NO_API`; after window
  creation `glfwCreateWindowSurface(rlvkGetInstance(), window, NULL, &surface)` →
  `rlvkAttachSurface(surface)`; `SwapScreenBuffer()` → `rlvkPresent()`;
  `FLAG_MSAA_4X_HINT` → `rlvkSetMsaaSamples(4)` BEFORE attach.
- Ship/locate `shaderc_shared` next to the binary (Windows: `shaderc_shared.dll` from the
  Vulkan SDK; Linux: distro package). Without it only the embedded default shader works —
  the game's ~30 custom shaders all need it. Must be new enough to export
  `shaderc_compile_options_set_vulkan_rules_relaxed` (2023+; **NDK's libshaderc lacks it**
  — for Android later, either ship a newer shaderc build or make that option optional-with-
  fallback in `RLVK_SHADERC_FUNCS` loading).

### 4.3 Port `compute/gpu_particle_system.c` from raw GL to the rl* compute API
The ONLY file in the engine calling `gl*` directly (glDispatchCompute, glBindBufferBase,
glBufferSubData, glUniform*, program compile). Map: program → `rlLoadShader(src,
RL_COMPUTE_SHADER)` + `rlLoadShaderProgramCompute`; glBindBufferBase → `rlBindShaderBuffer`
(indices 0..7); buffer create/update/read → `rlLoadShaderBuffer/rlUpdateShaderBuffer/
rlReadShaderBuffer`; glUniform → `rlGetLocationUniform` + `rlSetUniform` +
`rlEnableShader(program)`; dispatch+barrier → `rlComputeShaderDispatch` (barrier included).
The GLSL itself (`compute/shaders/gpu_particles.comp`, `#version 310 es`, std430 explicit
bindings 0/1) should compile via shaderc relaxed rules mostly as-is (bindings 0/1 fit the
0–7 SSBO range). Keep the GL path compiling for the GL build (`#if !defined(WUXING_USE_VULKAN)`
or migrate fully — decide with the module owner; `compute/` is the Compute Agent's per
`CLAUDE.md`). Note `rlSetVertexAttributeDivisor` is a no-op in rlvk (not needed — the
Vulkan draw path for particles should read the SSBO in the vertex shader via a descriptor,
which is exactly what Mali GLES couldn't do; SSBOs are created with VERTEX_BUFFER usage too
if a plain vertex-stream path is preferred).

### 4.4 Android (after 4.1 proves the backend on desktop)
- Instance ext `VK_KHR_android_surface` already handled (`__ANDROID__` branch);
  platform code must create the surface from `ANativeWindow` and drive
  `rlvkAttachSurface`/`rlvkPresent` through raylib's android platform.
- Lifecycle: surface loss on pause/resume → `rlvkRecreateSwapchain` machinery exists but
  Android also fully destroys the surface — needs a `rlvkDetachSurface`-style path
  (destroy sized objects + surface, re-attach on resume). Not written yet.
- shaderc on device: see 4.2 note; or precompile the game's shaders to SPIR-V offline at
  build time (better for weak devices anyway — no runtime compile hitches) and extend
  `rlLoadShaderProgram` to accept SPIR-V pairs (magic-number detect; the old rlLoadShader
  SPIR-V branch was removed, the program-level path is where it belongs).
- Real device matrix: the user's Samsung A33 (Mali-G68, VK 1.1) is the reference target.
  Driver-quality bugs are expected — budget for it.

### 4.5 Smaller known gaps (deliberate, documented in code)
- `rlLoadShaderProgramEx` returns INVALID (separate VS/FS ids → program; raylib's normal
  flow uses `rlLoadShaderProgram(vsCode, fsCode)` which is fully implemented — only exotic
  callers need Ex).
- `rlBindImageTexture` records, but textures created via `rlLoadTexture` lack
  `VK_IMAGE_USAGE_STORAGE_BIT` and are never transitioned to `VK_IMAGE_LAYOUT_GENERAL` —
  imageLoad/Store consumers need a dedicated creation path (add usage bit + layout
  handling when a real consumer appears; adding STORAGE to all textures was rejected:
  disables UBWC/compression on mobile).
- Compute sampler units limited to bindings 12–13 (2 samplers per compute shader).
- `RLVK_MAX_RENDER_PASSES 32` / `RLVK_MAX_CACHED_FRAMEBUFFERS 64` / pool sizes 1024/256 —
  warn loudly when exhausted; tune when real content runs.
- Perf note in the file header: the 1.3-design benchmark claim (1.5–7.5x vs GL on 17/19
  scenes) predates the retarget; render-pass/staging/descriptor-ring paths are NOT
  re-benchmarked.
- Debug-messenger drain TODO at ~line 2871 (cosmetic).

### 4.6 Long-term (the standalone-engine vision)
After the backend is proven: engine-side work is tiler-aware VFX (the wuxing post chain —
HDR, bloom MRT, soft particles reading depth — is bandwidth-heavy; render-pass load/store
ops now exist as real levers), then extracting `core/` VFX + `compute/` + `environment/`
into a library whose only downward interface is rlgl + the platform hooks. Renderer
selection GL-vs-Vulkan stays a build-time choice per binary until someone needs a
single-binary launcher (the two implementations define the same `rl*` symbols — runtime
selection requires a renderer shared-library split; deliberately deferred).

## 5. How to verify any change

```bash
./scripts/check_rlvk_compile.sh        # compile-check (the only loop available on this Mac)
./scripts/gen_rlvk_shaders.sh          # only if the default .vert/.frag changed
```
Keep every intermediate state compiling. The strangler-pattern used throughout (Caps flag →
fallback lands → old requirement deleted) kept the tree coherent; the transitional
`RLVK_STILL_REQUIRED` guard block in `rlvkInitLogicalDevice` is now GONE — do not
reintroduce hard feature requirements without a fallback.

## 6. Architecture rules to preserve (decisions, not accidents)

1. **One code path.** 1.1-core is THE path; 1.3-era features are dispatch-table fast paths
   only where the shim pattern makes them free (sync2, push descriptors). Do not add
   `if (Caps.x)` forks in draw-path logic.
2. **Shims live in the `vk.` dispatch table**, call sites keep the modern shape.
3. **Layout tracking is manual** (`rlvkTextureSlot.currentLayout` + explicit barriers);
   render passes must never change layouts (initial==final always).
4. **Fence-gated lifetime** for everything transient: dead-resource ring (now also
   framebuffers), per-frame pools reset at the two cb-reset points (`rlvkFlushFrame`
   post-wait and `rlvkBeginFrame` post-wait) — if you add a per-frame resource, reset it
   at BOTH points.
5. **Clip-z is a shader concern** — never re-add depth_clip_control (double-transform trap,
   §3.1).
6. **SPIR-V target stays vulkan1.1** in `rlvkCompileGlsl` and `gen_rlvk_shaders.sh`.
7. All GLSL compilation funnels through `rlvkCompileGlsl` — that is the single place for
   source-level transforms (sanitizer, clip-z epilogue, binding bases).
