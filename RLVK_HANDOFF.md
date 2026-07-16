# RLVK Vulkan 1.1 Backend — Handoff Document

> **Purpose**: complete context for an AI (or human) continuing this work with zero prior
> conversation. Read this top to bottom before touching anything under `third_party/vulkan/`.
> Companion: `third_party/vulkan/CLAUDE.md` (the Renderer Agent's operating rules — fragment
> map, verification ladder, token rules). Last major update 2026-07-16, after the windowed
> platform layer landed, the game ran on Vulkan, and the first real-content bug wave was
> root-caused and fixed.

---

## 1. Vision & why this exists

The user's goal (their words, translated): **many machines are stuck on Vulkan 1.1; if we
solve this, the engine has a real future.** Not just particles — beautiful VFX must run on
old/weak devices. Long-term: extract a standalone renderer/engine, not wuxing-game-only.

Tiering:
- **Old desktop + (for now) Android → existing OpenGL/GLES backend** (unchanged).
- **Vulkan 1.1+ devices → rlvk**, one single code path from weakest 1.1 Android driver to
  newest desktop GPU. 1.3 features are *optional accelerators only* where trivially gated.
- Key motivator: **compute particles on Android**. Mali GLES silently fails vertex-stage
  SSBO reads (`CORE_ISSUES.md` Item 5). Vulkan *mandates* vertex-stage storage-buffer reads
  on every conformant device.

## 2. What rlvk is

`third_party/vulkan/rlvk.h` implements **the complete rlgl API on Vulkan** — same `rl*`
functions, different rasterizer. rlgl.h is included verbatim and never modified. The game
keeps calling raylib/rlgl normally.

**File layout:** `rlvk.h` is a ~350-line umbrella (public decls + impl preamble) ending in
a fixed-order `#include` chain of 14 fragments under `third_party/vulkan/rlvk/*.inl`. The
fragments are **textual includes of the ONE `RLVK_IMPLEMENTATION` translation unit** — no
include guards, order significant, statics span them. Never include a fragment directly,
never reorder the chain. The per-fragment content map lives in
`third_party/vulkan/CLAUDE.md` — grep the symbol, read only that fragment region.

Integration model: in the ONE translation unit that would define `RLGL_IMPLEMENTATION`
(raylib's rcore.c), define `RLVK_IMPLEMENTATION` and include `rlvk.h` instead — done by
`scripts/rlvk_patch_raylib.py` (idempotent, marker-guarded), invoked from CMake when
`WUXING_USE_VULKAN=ON`. `GRAPHICS_API_OPENGL_33` stays defined ONLY to fix rlgl.h's
`rlVertexBuffer` struct layout. Never combine both implementations in one build
(`#error`-guarded).

Platform hooks (NOT part of rlgl's API): platform creates `VkSurfaceKHR` →
`rlvkAttachSurface(surface)`; `SwapScreenBuffer()` → `rlvkPresent()`;
`rlvkSetMsaaSamples(4)` before attach; `rlvkGetInstance()` for `glfwCreateWindowSurface`.

## 3. Current state (2026-07-16)

- **Retarget 1.3 → 1.1-core: COMPLETE.** Only hard requirement: core 1.1 + `VK_KHR_swapchain`.
- **Headless paths: 20/20 runtime-verified** on MoltenVK w/ validation
  (`scripts/run_rlvk_runtime_test.sh`).
- **Windowed platform layer: LANDED.** The full game builds and runs under Vulkan on macOS
  (`WUXING_USE_VULKAN=ON`; MoltenVK is the dev proxy for "weak 1.1 driver" — macOS itself
  ships GL in production).
- **Visual test suite: 10/10** (`scripts/run_rlvk_visual_test.sh`, see §5).
- **First real-content bug wave: root-caused and fixed** — every case is documented in §7.
  Fixed: opaque-square particles (stale pipeline on failed build), device-lost after heavy
  VFX (unchecked acquire), no depth occlusion inside render textures = character
  see-through + black-hole-over-ring (MoltenVK/Intel `SAMPLED`-depth quirk).
- Reference hardware so far: MoltenVK 1.2.11 / Intel Iris 6000 / macOS 12 (device API 1.2,
  no native sync2 → the 1.1 fallback paths run for real). Android device target: Samsung
  A33 (Mali-G68, VK 1.1) — not yet run.

### 3.1 The conversion table (what replaced what)

| Old 1.3 requirement | Replacement (single path unless noted) | Where |
|---|---|---|
| `dynamicRendering` | Cached `VkRenderPass` + `VkFramebuffer` (bounded tables keyed by `rlvkRenderPassKey` / pass+views+extent). All scope opens via `rlvkBeginScopeRenderPass()`. Attachment layouts initial==final so passes never fight manual layout tracking; no subpass dependencies (explicit barriers outside). Attachment order everywhere: colors, resolve, depth. | `rlvk_renderpass.inl` |
| `synchronization2` | Shim installed into the `vk.` dispatch table when sync2 absent: `rlvkCmdPipelineBarrier2Compat` / `rlvkQueueSubmit2Compat` translate to core sync1. Call sites keep the sync2 shape. Bit mapping: COPY/BLIT/RESOLVE/CLEAR→TRANSFER, INDEX/VERTEX_ATTRIBUTE_INPUT→VERTEX_INPUT, SAMPLED/STORAGE_READ→SHADER_READ. | `rlvk_config.inl` |
| `VK_EXT_host_image_copy` | Removed. Staging buffer + one-shot TRANSIENT-pool submissions (`rlvkOneShotBegin/End`, `rlvkStagingUploadImage/ReadImage`, `rlvkCmdTransitionImage`). | `rlvk_texture.inl` |
| `VK_KHR_push_descriptor` | Optional. CPU shadow + per-frame descriptor-pool ring; `rlvkFlushSet0` before every draw site. Native push descriptors when available. | `rlvk_frame.inl` |
| `VK_EXT_depth_clip_control` | Removed **everywhere, deliberately**. GL [-1,1] clip-z remapped by vertex-shader epilogue `gl_Position.z = (z + w) * 0.5` in TWO places that must stay in sync: injected by `rlvkInjectClipZEpilogue` for every runtime-compiled VS, and baked into `shaders/rlvk_default.vert`. **Trap: re-enabling the extension without removing the epilogue double-transforms depth.** | `rlvk_shaderc.inl` |
| `VK_EXT_vertex_attribute_divisor` | Removed. Missing-attribute broadcasts = `stride 0, rate VERTEX` (core semantics); real instancing = plain INSTANCE rate. See §7.5 for the MoltenVK stride-0 workaround. | `rlvk_pipeline.inl` |
| `bresenhamLines`/`wideLines`/`fillModeNonSolid` | Optional Caps with graceful degradation (default raster / lineWidth 1.0 / FILL). | `rlvk_pipeline.inl` |
| 1.3-only cmd variants (`SetViewportWithCount`, `WriteTimestamp2`) | Plain 1.0 calls. | pipeline dynamic state |
| shaderc target vulkan 1.3 | **vulkan 1.1 / SPIR-V 1.3** — 1.3 target emits SPIR-V 1.6, invalid on 1.1 drivers. Load-bearing. | `rlvkCompileGlsl` |

### 3.2 Beyond the retarget

- **Swapchain recreation** on OUT_OF_DATE/SUBOPTIMAL + minimize handling (swapchain=NULL →
  skip frames). `rlvkAttachSurface` is re-entrant.
- **Compute fully implemented**: fixed set-0 layout (bindings 0–7 SSBO, 12–13 sampler,
  14 loose-uniform UBO — **NO storage images, see §7.4**), GL bind-then-dispatch semantics,
  render-scope suspension around dispatch, one-shot path outside frames. SSBOs get
  VERTEX_BUFFER usage too (particle draws may read them as vertex streams).
- **shaderc dlopen** for all platforms; vendored upstream `shaderc.h` (NDK's lacks
  `set_vulkan_rules_relaxed`).
- **MoltenVK enumeration** (`VK_KHR_portability_enumeration` + subset ext when present).

## 4. Key files

| File | What |
|---|---|
| `third_party/vulkan/rlvk.h` + `rlvk/*.inl` | The backend (umbrella + 14 fragments). |
| `third_party/vulkan/rlvk_shaders.h` | Generated embedded SPIR-V default shader. Regen: `scripts/gen_rlvk_shaders.sh` (glslc, `--target-env=vulkan1.1`). |
| `third_party/vulkan/shaders/rlvk_default.vert/.frag` | Default-shader source. Contract: attrib locations 0/1/3, push_constant == `rlvkPushConstants{mat4 mvp; vec4 colDiffuse}`, set0 binding0 = texture0, clip-z epilogue baked in. |
| `third_party/vulkan/tests/rlvk_runtime_test.c` | Headless suite (20 checks). |
| `third_party/vulkan/tests/rlvk_visual_test.c` | Windowed scenario suite (10 scenarios, self-checking pixels). **Every draw-path bug fix adds a scenario here first.** |
| `scripts/check_rlvk_compile.sh` | Tier-1 compile check (no SDK needed). |
| `scripts/run_rlvk_runtime_test.sh` | Tier-2 headless + validation. |
| `scripts/run_rlvk_visual_test.sh` | Tier-3 windowed; `VALIDATE=1` adds layers; caches a Vulkan-patched raylib in `/tmp/rlvk_visual_cache` (first run ~2 min, then ~20 s). |
| `scripts/rlvk_patch_raylib.py` | Patches raylib 6.0 for Vulkan (rcore.c impl swap + GLFW NO_API/surface/present). Idempotent. |
| `third_party/vulkan/CLAUDE.md` | Renderer Agent rules. |

## 5. How to verify any change (MANDATORY ladder, cheapest first)

```bash
./scripts/check_rlvk_compile.sh                  # 1. seconds, after EVERY edit
./scripts/run_rlvk_runtime_test.sh               # 2. init/compute/upload changes
./scripts/run_rlvk_visual_test.sh [scenario]     # 3. anything touching draw/present/depth/blend
cmake --build build && ./build/wuxing            # 4. HUMAN-run, final confirmation only
```

**Never start at tier 4.** Debugging via the full game burned entire sessions before the
suite existed; every bug in §7 reproduces in a ≤40-line scenario that runs in seconds.
Scenario list: `clear batch_alpha additive3d shader_uniform depth depth_rt winding_rt
instanced readback stress` (`--list`). Each guards the bug class named in its comment.

## 6. DEBUGGING METHODOLOGY — read this before your first bug hunt

Hard-won process lessons from the bring-up sessions. Following these would have saved the
majority of all debugging time spent so far.

1. **Reproduce in a micro-scenario before reading code.** For the FBO-depth bug (§7.1),
   hours of careful code reading found *nothing* — every struct was correct — while four
   env-gated experiments found the driver quirk in minutes. Reading tells you what the code
   *intends*; only execution tells you what the driver *does*. Write/pick a visual-test
   scenario first, always.

2. **Zero validation errors does NOT mean correct.** Three separate silent bugs: the
   SAMPLED-depth quirk (§7.1, silently no depth test), stale-pipeline draws (§7.2, silently
   wrong shader), MoltenVK UBO zeroing (§7.4, silently zero uniforms). Validation catches
   *illegal* Vulkan, not *wrong* Vulkan. The inverse also holds: a wall of validation
   errors usually has ONE root cause — find the FIRST failure in the chain (§7.3: eight
   scary sync VUIDs were all downstream of one unchecked `vkAcquireNextImageKHR`).

3. **Bisect with single-variable, env-gated experiments compiled into the backend.**
   Pattern used for §7.1: `getenv("RLVK_EXP_X") ? variantA : variantB` at the suspect site,
   rebuild (~20 s), rerun the one failing scenario. Three runs isolated one usage flag.
   **Remove the gates once the answer is known** — permanent switches rot.

4. **Distrust your own probes.** Two self-inflicted detours: (a) pixel probes placed at
   wrong coordinates reported "instancing broken" when rendering was perfect — *view the
   actual screenshot image* before believing a numeric probe; (b) a depth-visualization
   probe sampled the depth texture in the wrong layout and returned garbage that looked
   like evidence. When a probe contradicts other evidence, validate the probe first.
   (Same spirit as the project-wide rule: trust visuals over numeric PASS.)

5. **Fail visibly-safe, never silently-garbage.** A failed pipeline build used to leave
   the previous pipeline bound and draw anyway → soft-alpha particles rasterized with the
   wrong shader as opaque squares (§7.2) — *worse than invisible*, because the symptom
   (squares) pointed at blending/alpha, nowhere near the cause. All 3 draw sites now skip
   the draw when `rlvkBindPipeline` returns false. Preserve this property in new code:
   an error path must produce nothing, not leftovers.

6. **Driver quirks get a Caps flag + a repro scenario, not an inline hack.** Every quirk in
   §7 is (a) detected at init into `RLVK.Caps.*` or handled by a single guarded site,
   (b) documented with the bisection evidence, (c) covered by a test scenario. Never
   "fix" a driver issue with an unconditional behavior change that penalizes healthy
   drivers, and never leave it as folklore in a chat log.

7. **Suspect state *lifecycles* before state *values*.** The GPU-fault class (§7.3, §7.6)
   all came from ring/lifecycle desync (fences, semaphores, frameCounter, frameConsumed) —
   the individual values were always "plausible". When you see device-lost or
   command-buffer-in-use errors, audit who advances the ring and every early-return between
   acquire and present.

## 7. CASE STUDIES — every real bug so far, with the chain that found it

Each entry: symptom → what it *looked* like → actual root cause → fix → guard. Future
bugs will rhyme with these. **Check this list before starting a new hunt.**

### 7.1 No depth occlusion inside render textures (character see-through, black hole not occluding its ring)
- **Symptom**: character limbs visible through clothing; VFX behind an opaque core drawn
  over it. Only in the real game — because the game renders its whole scene through
  PostFX's render texture.
- **Looked like**: bad depth state in pipelines / missing depth attachment / winding.
  Every one of those was read and verified CORRECT (pipeline key depthTest=1, pass had the
  attachment, clear reached 1.0, framebuffer had the view). Validation: **silent**.
- **Diagnostic chain**: `depth` scenario (swapchain) PASSED vs `depth_rt` (render texture)
  FAILED → probes showed color writes landing, depth writes not → usage-flag bisection via
  env gates: `ATT` PASS / `ATT|SAMPLED` FAIL / `ATT|TRANSFER_SRC` PASS.
- **Root cause**: **MoltenVK/Intel quirk — creating a depth image with
  `VK_IMAGE_USAGE_SAMPLED_BIT` silently disables depth test/write on that attachment.**
- **Fix**: `Caps.noSampledDepth` (portability + vendorID 0x8086, `rlvk_frame.inl`), FBO
  depth images drop SAMPLED under the quirk (`rlLoadTextureDepth`). Trade-off recorded:
  depth-sampling consumers (soft particles, screen-distortion depth probe) lose their
  input on this driver; if that matters, implement a shadow-copy (attachment-only depth +
  `vkCmdCopyImage` to a sampleable twin at scope close).
- **Guard**: `depth_rt` scenario.

### 7.2 Particles/VFX as opaque squares (black borders that should be transparent)
- **Symptom**: any soft-alpha billboard/VFX quad renders as a hard square.
- **Looked like**: broken alpha blending or texture alpha. All blend paths tested fine in
  isolation — the misdirection cost a full session of blend/texture experiments.
- **Root cause**: `rlvkBindPipeline` returns false when a pipeline fails to build (e.g.
  the GPU-particle shader reading an SSBO in the vertex stage — unsupported in the graphics
  set0 layout), but all 3 draw sites ignored the return and issued the draw **with the
  previous draw's pipeline still bound** → geometry rasterized under the wrong shader.
- **Fix**: all 3 draw sites (`rlvk_core.inl` batch flush, `rlvk_texture.inl` rlvkDrawMesh,
  `rlvk_compute.inl` quad blit) skip the draw on bind failure. Failed shader = invisible,
  not garbage.
- **Still open behind it**: graphics-stage SSBO support (§8.2) so GPU particles actually
  *render* instead of being cleanly skipped.
- **Guard**: validation `VUID-vkCmdDraw-None-08606` count == 0; the skip logic itself.

### 7.3 Device lost (GPU timeout) after heavy VFX ran a while
- **Symptom**: crash with a wall of sync validation errors — fences in use, command
  buffers pending, semaphores double-signaled, "image not acquired".
- **Looked like**: deep frame-ring corruption; every error individually suggested a
  different fix.
- **Root cause**: ONE unchecked call. `vkAcquireNextImageKHR`'s result was only handled
  for OUT_OF_DATE; any other failure fell through with `imageIndex` still 0 and the
  acquire semaphore unsignaled → present of a never-acquired image + a queue wait on a
  semaphore nothing would ever signal → GPU stall → driver kills the device → every ring
  object downstream desyncs (that's where the *other eight* VUIDs came from).
- **Fix**: bail out of `rlvkBeginFrame` on any acquire result other than
  SUCCESS/SUBOPTIMAL, **before** `vkResetFences` (fence stays signaled, `frameActive`
  stays false, counter doesn't advance — next frame retries cleanly).
- **Lesson**: in a VUID avalanche, sort by causality, not scariness.
- **Guard**: `stress` scenario (arena-exhaustion mid-frame flush path) + `readback`.

### 7.4 Compute UBO reads all zeros (MoltenVK)
- **Root cause** (bisected with a raw-Vulkan repro, no rlvk code involved): merely
  DECLARING storage-image bindings in a compute descriptor-set layout makes a UBO at a
  later binding read zeros on MoltenVK/Intel.
- **Fix**: the fixed compute layout has NO storage-image bindings (SSBO 0–7, samplers
  12–13, UBO 14). Images will need their own descriptor SET when a consumer appears.
- **Related shaderc trap**: `auto_bind_uniforms` rebases even explicitly-bound UBOs —
  compute shaders must use loose uniforms; explicit std430 SSBO bindings are safe.

### 7.5 Vertex attribute fetch reads zeros / GPU timeout on tiny dummy buffer (MoltenVK)
- Two related MoltenVK facts, both already encoded — keep them:
  (a) **Metal resolves vertex-buffer indices through the bound pipeline** → the pipeline
  must be bound BEFORE `vkCmdBindVertexBuffers` at every draw site (comments mark this).
  (b) portability subset rejects `stride < format size` → the stride-0 broadcast dummy
  buffer has an `__APPLE__` workaround (large buffer + real stride).

### 7.6 Present/readback lifecycle faults
- `TakeScreenshot` after `EndDrawing` left `frameConsumed` armed → the NEXT frame's
  present got skipped mid-recording → frameCounter advanced under it → commands landed in
  a never-begun command buffer → GPU fault. Fix: a freshly-begun frame clears the flag.
- Post-present readbacks read the PREVIOUS slot's intermediate image (STOREd, still
  TRANSFER_SRC) instead of opening a fresh frame that would only contain a clear.
- **Guard**: `readback` scenario.

### 7.7 False alarms to not re-chase
- **Winding/frontFace is CORRECT** (`frontFace = CLOCKWISE` + GL-CCW geometry under the
  y-down convention = GL parity; verified: CCW visible, CW culled, `winding_rt` scenario).
  An early "culled front face" result was a probe bug, not a backend bug.
- **`GenImageGradientRadial` sprites are opaque-alpha by design** — they only look right
  under ADDITIVE blend; a black square from one of these under ALPHA blend is a *caller*
  bug (wrong blend mode), not a backend alpha bug. All backend blend paths are verified by
  `batch_alpha`/`additive3d`/`shader_uniform`.

## 8. What remains

### 8.1 Confirm in-game (task open)
User rebuilds the game → verify character self-occlusion + black-hole occlusion fixed by
§7.1. If depth-sampling VFX (soft particles, screen distortion) visibly degrade on the
quirk driver, implement the shadow-copy depth described in §7.1.

### 8.2 Graphics-stage SSBO (GPU particles render path)
`core/shaders/particles.vs` reads a std430 SSBO by `gl_InstanceID`. Today that pipeline
fails to build (cleanly skipped per §7.2) because graphics set0 has only samplers+UBOs.
Needed: SSBO bindings in the graphics set0 layout + `rlBindShaderBuffer` wiring for the
graphics path + shaderc storage-buffer binding base for VS/FS (avoid Metal index collision
— MoltenVK errored with "cannot reserve buffer resource location index 9") + enable
`vertexPipelineStoresAndAtomics` feature (or inject NonWritable decoration). Repro exists:
scratchpad `rlvk_ssbo_vs.c` pattern; add a `ssbo_vs` scenario when implementing.

### 8.3 Port `compute/gpu_particle_system.c` from raw GL to the rl* compute API
The only engine file calling `gl*` directly. Map: program → `rlLoadShader(src,
RL_COMPUTE_SHADER)` + `rlLoadShaderProgramCompute`; glBindBufferBase →
`rlBindShaderBuffer` (0..7); buffers → `rlLoadShaderBuffer/rlUpdateShaderBuffer/
rlReadShaderBuffer`; dispatch+barrier → `rlComputeShaderDispatch`. GLSL
(`compute/shaders/gpu_particles.comp`, 310 es, std430 bindings 0/1) should compile via
relaxed rules. Coordinate with the Compute Agent (module owner). Depends on §8.2 for the
draw half.

### 8.4 Android
- `VK_KHR_android_surface` branch exists; platform code must drive
  `rlvkAttachSurface`/`rlvkPresent` from `ANativeWindow`.
- Pause/resume destroys the surface — needs a `rlvkDetachSurface`-style path (not written).
- shaderc on device: ship a new-enough libshaderc or precompile shaders to SPIR-V offline
  (better for weak devices anyway) and extend `rlLoadShaderProgram` to accept SPIR-V pairs.
- Expect Mali driver quirks; §6's methodology applies verbatim — build the scenario first.

### 8.5 Smaller known gaps (deliberate)
- `rlLoadShaderProgramEx` unimplemented (nothing in raylib's normal flow uses it).
- `rlBindImageTexture` records but plain textures lack STORAGE usage/GENERAL layout —
  dedicated creation path when a real consumer appears (blanket STORAGE rejected: kills
  mobile framebuffer compression).
- Compute samplers limited to 2 (bindings 12–13).
- Cache limits warn loudly when exhausted (`RLVK_MAX_RENDER_PASSES` 32, framebuffers 64,
  desc sets 1024/256) — tune against real content.
- Perf not re-benchmarked since the retarget; old 1.3-era claims in the file header are
  stale.

### 8.6 Long-term (standalone engine)
Tiler-aware VFX (load/store ops are now real levers), then extract `core/` VFX +
`compute/` + `environment/` into a library whose only downward interface is rlgl + the
platform hooks. GL-vs-Vulkan stays a build-time choice per binary (both define the same
`rl*` symbols; runtime selection needs a shared-library split — deliberately deferred).

## 9. Architecture rules to preserve (decisions, not accidents)

1. **One code path.** 1.1-core is THE path; 1.3 features are dispatch-table fast paths
   only (sync2, push descriptors). No `if (Caps.x)` forks in draw-path logic — quirk Caps
   (§7) gate *resource creation*, not draw logic.
2. **Shims live in the `vk.` dispatch table**; call sites keep the modern shape.
3. **Layout tracking is manual** (`currentLayout` + explicit barriers); render passes never
   change layouts (initial==final always).
4. **Fence-gated lifetime** for everything transient (dead-resource ring, per-frame pools
   reset at BOTH cb-reset points: `rlvkFlushFrame` post-wait and `rlvkBeginFrame`
   post-wait). New per-frame resources must reset at both.
5. **Clip-z is a shader concern** — never re-add depth_clip_control (double-transform trap).
6. **SPIR-V target stays vulkan1.1** in `rlvkCompileGlsl` and `gen_rlvk_shaders.sh`.
7. **All GLSL compilation funnels through `rlvkCompileGlsl`** — the single place for
   source transforms (clip-z epilogue, binding bases, location canonicalization).
8. **Error paths draw nothing** (§7.2). A failed bind/build/acquire skips the operation
   and leaves ring state consistent; it never proceeds with stale handles.
9. **Every draw-path bug fix ships with a visual-test scenario** that failed before the fix
   and passes after. The suite is the backend's memory.
