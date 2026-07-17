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
- **Visual test suite: 12/12** (`scripts/run_rlvk_visual_test.sh`, see §5).
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
| `third_party/vulkan/tests/rlvk_visual_test.c` | Windowed scenario suite (12 scenarios, self-checking pixels). **Every draw-path bug fix adds a scenario here first.** |
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
Scenario list: `clear batch_alpha additive3d shader_uniform depth depth_rt soft_depth
winding_rt instanced ssbo_vs readback stress` (`--list`). Each guards the bug class named
in its comment.

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
  depth images drop SAMPLED under the quirk (`rlLoadTextureDepth`). The depth-sampling
  trade-off it created (soft particles, screen-distortion depth probe) is now **resolved by
  the shadow-copy twin — see §7.10**.
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

### 7.7 Invisible GPU particles — a masterclass in confounded bisection
- **Symptom**: GPU-particle pool counts up, particles fully invisible. Zero validation
  errors, pipeline builds, SSBO descriptor pushed with the right buffer, buffer content
  verified correct by readback.
- **Actual root causes (three, stacked)**: (1) the game's draw shader had been switched to
  per-instance attributes + `rlSetVertexAttributeDivisor` ("VBO instancing bypass") which
  rlvk doesn't support — attributes read zeros → the shader's own liveness guard culled
  every particle (fixed by restoring the SSBO+`gl_InstanceID` read the backend now
  supports); (2) latent: the mid-frame buffer-upload barrier only covered
  vertex-attribute/index reads, not shader storage reads (widened — correct fix even
  though it wasn't the visible breaker); (3) the new quad template copied the old
  CORNER_TABLE vertex order, which is **CW = backface-culled** — the entire final mystery
  was winding, nothing exotic.
- **Bisection lessons paid for in hours**:
  - **A control that can't fail is not a control.** Twice: a variant "proved" mid-frame
    updates worked while its buffer slot was REUSED from a previous sub-test still holding
    identical stale data; another update-test wrote data identical to the init data, so
    delivery failure was undetectable. Make controls fail loudly: unique payloads per
    variant, fresh processes, NULL-init when testing delivery.
  - **One mutation per variant, one variant per process.** In-process sub-tests contaminate
    each other through slot reuse and cached state.
  - **Instrument the error you can't see**: `vkCreateGraphicsPipelines`' result was silently
    dropped (now TRACELOGged) — combined with the (correct) skip-on-failed-bind rule,
    a failed pipeline was pure silent invisibility.
  - Debug flags added: `RLVK_DEBUG_SSBO` (rebase mask + bind pushes), post-rebase SPIR-V
    dump via `RLVK_DUMP_SPV` (`rlvk_rebased_vs.spv`), `RLVK_DEBUG_PIPE` (pipeline-key at
    bind), `VKSCOPE` under `RLVK_DEBUG_PIPE` (FBO scope opens).
- **Guard**: `ssbo_vs` scenario now uses the game's exact pattern (NULL-init buffer +
  mid-frame `rlUpdateShaderBuffer` + instanced draw); e2e repro with the real game shaders
  lives in the session scratchpad pattern (LoadShader of `compute/shaders/gpu_particles_ssbo.vs`).
- **Epilogue**: after all three fixes the game STILL showed nothing — the in-game
  `RLVK_DEBUG_SSBO` log proved the whole chain ran (compile mask 0x1, bind mask 0x1), and
  the residual invisibility was *content*: vfx_test spawn radii (0.06 m / 0.008 m) are
  sub-pixel at arena camera distance; this machine had never run the GPU path before
  (macOS GL = CPU path), so those numbers were never visibility-tuned. **Once
  instrumentation proves the pipeline executes end-to-end, check data magnitudes
  (meter scale!) before resuming the code hunt.**
- **The final four layers** (found by escalating in-game data probes — read back particle 0
  every 30 frames, then project it through the draw's own MVP):
  4. **`rlUnloadShader(cs)` after `rlLoadShaderProgramCompute(cs)` destroyed the program**:
     GL stage-delete-after-link is harmless, but rlvk's compute STAGE slot IS the program
     slot — the unload freed it, the next shader load RECYCLED it, `rlEnableShader(prog)`
     activated the wrong shader, and every dispatch silently no-opped (probe signature:
     life/pos FROZEN with active=1). Fixed in rlvk: `rlUnloadShader` ignores linked compute
     programs; `rlUnloadShaderProgram` does the real destroy; the dispatch early-return now
     WARNS once naming the reason.
  5. **State.modelview was IDENTITY at the particle draw's callsite** (probe signature:
     clip.w == −z_world exactly). Something mid-pass resets it under Vulkan only (open task:
     root-cause; CPU particles at the same site still affected). Worked around by deriving
     MVP from the function's own camera param — and the projection had to replicate
     **MyBeginMode3D's exact `rlFrustum(near=1.0, far=1000.0)`**, not
     `rlGetCullDistanceNear/Far`: perspective X/Y is near-independent but DEPTH is not, so
     the mismatch made particles lose the depth test wherever ground pixels covered them
     ("visible only off the edge of the ground").
  6. **`rlDisableTexture()` had wrong semantics** (reset the batch's current texture instead
     of clearing the ACTIVE UNIT's binding like `glBindTexture(unit, 0)`): a vector-field
     texture bound at unit 0 for a compute dispatch poisoned every later draw's texture0 —
     square, lemon-yellow particles after pressing VF test once. Fixed for 2D + cubemap.
  7. **Test content again**: both vfx_test force fields were built ONCE with origins frozen
     at the first press's player position, and `FORCE_VECTOR_TEXTURE` is a HARD BOX whose
     half-extent was 0.3 m against a ±0.8 m spawn line — "physics depends on where you
     stand" and "particles frozen in place" were the field's geometry, not the backend.
     Fields now rebuild per press with a box covering the spawn line.
- **Final lesson**: one symptom ("invisible particles"), SEVEN stacked causes spanning
  shader authoring, API-semantics parity (twice), ambient matrix state, frustum constants,
  and test content. None was guessable from the symptom; every one fell to a probe that
  measured the *next* link in the chain (SSBO mask → bind → buffer content → life/pos over
  time → NDC through the real MVP). When a fix doesn't change the symptom, the fix was
  still usually right — re-probe, don't revert.

### 7.8 False alarms to not re-chase
- **Winding/frontFace is CORRECT** (`frontFace = CLOCKWISE` + GL-CCW geometry under the
  y-down convention = GL parity; verified: CCW visible, CW culled, `winding_rt` scenario).
  An early "culled front face" result was a probe bug, not a backend bug.
- **`GenImageGradientRadial` sprites are opaque-alpha by design** — they only look right
  under ADDITIVE blend; a black square from one of these under ALPHA blend is a *caller*
  bug (wrong blend mode), not a backend alpha bug. All backend blend paths are verified by
  `batch_alpha`/`additive3d`/`shader_uniform`.

### 7.9 `VUID-...-oldLayout-01211` ×30 in the validated suite (two independent causes)
- **Symptom**: 30 `01211` errors under `VALIDATE=1` (never a wrong pixel — all scenarios
  passed; pure validation noise, the §8.5 leftover).
- **Cause A (minor)**: the FBO **color** scope-open barrier (`rlvk_renderpass.inl`
  `rlEnableFramebuffer`) hardcoded `oldLayout = SHADER_READ_ONLY`, but a freshly-created FBO
  color texture is still `UNDEFINED` on its first bind → oldLayout mismatch. Fix: use the
  **tracked** `currentLayout` as oldLayout (skip if already `COLOR_ATTACHMENT`), exactly like
  the depth barrier beside it. `UNDEFINED` is a legal oldLayout; `SHADER_READ` is not when the
  image was never in it.
- **Cause B (the ×30 bulk)**: `rlDisableFramebuffer` transitioned the FBO **depth** image to
  `SHADER_READ_ONLY_OPTIMAL` (so depth shaders could sample it) **unconditionally** — but under
  `Caps.noSampledDepth` (§7.1) that image is created WITHOUT `SAMPLED` usage, and a transition
  to/from `SHADER_READ_ONLY` is illegal for a non-sampleable image (the VUID text is the
  usage-compat clause, not a raw layout-tracking mismatch — a giveaway that the image lacks
  `SAMPLED`, not that `currentLayout` was wrong). Fix: gate that transition on
  `!Caps.noSampledDepth`; leave the depth in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (it can never
  be sampled on the quirk driver anyway), which also makes the scope-open depth guard skip it.
- **Lesson**: `01211`'s message body distinguishes the two — "oldLayout X not compatible with
  the image's *current layout*" = tracking bug (Cause A); "...not compatible with the image's
  *usage flags*" = you're moving to a layout the image's usage forbids (Cause B). Read which.
- **Guard**: `depth_rt` + `winding_rt` already exercised both paths (they PASSED on pixels
  before, emitted the VUIDs); the fix is confirmed by the suite's `01211` count dropping 30→0.

### 7.10 Soft particles hard-cut against geometry under `Caps.noSampledDepth` (shadow-copy twin)
- **Symptom**: additive glows sliced by a hard edge where they meet scene geometry (the dark
  sphere / arena), instead of fading — the classic "no soft particles" look. Real game only.
- **Root cause**: §7.1 drops `SAMPLED` from FBO depth on the quirk driver, so the game's
  `core/shaders/depth_copy.fs` (samples `renderTex.depth` → linearized R32F → soft-particle
  fade in `core/shaders/common/soft_particle.glsl`) sampled the **white default** substitute →
  depth reads as "infinitely far" → fade factor ≈ 1 → no fade → hard cut. (After the §7.9 fix
  the depth rests in `DEPTH_ATTACHMENT`, so `rlvkPushTexture`'s "not `SHADER_READ` ⇒ default"
  substitution kicks in cleanly — same visible result, no validation error.)
- **Fix**: the §7.1 shadow-copy twin, done entirely in rlvk (game code untouched). Under the
  quirk `rlLoadTextureDepth` also creates a sampleable **R32_SFLOAT color** twin + a scratch
  buffer; `rlDisableFramebuffer` bounces the raw depth `depth-image → sampleScratch → twin` at
  scope close; `rlvkPushTexture` routes any sample of that texture slot to the twin's
  view/layout. The whole `rl*` surface is unchanged — `renderTex.depth` just becomes sampleable.
- **Two traps paid for in bisection**:
  - **The twin must be COLOR, not depth.** A D32 twin + `vkCmdCopyImage` (depth→depth) is
    valid and validation-clean, but **Metal cannot sample a depth-format texture through a
    plain GLSL `sampler2D`** — an env-gated `vkCmdClearDepthStencilImage(twin, 0.5)` control
    still sampled as ~1.0, proving the sampler, not the copy, was the broken link. R32F color
    samples correctly everywhere; `depth_copy.fs` already treats the value as raw NDC depth.
  - **Aspect crossing.** `vkCmdCopyImage` can't copy DEPTH→COLOR aspect, so the raw bytes
    (D32 and R32 are both 4 B/texel) bounce through the scratch buffer:
    `vkCmdCopyImageToBuffer` (DEPTH) then `vkCmdCopyBufferToImage` (COLOR), one buffer barrier
    between them.
- **Guard**: `soft_depth` scenario — renders a near cube into an RT, samples `rt.depth` in a
  shader, linearizes, and asserts the center reads a near distance (not the far default).
- **Confirmed in-game (2026-07-17)**: arena glows fade softly into the background; the hard
  cut against scene geometry is gone.

## 8. What remains

### 8.1 Confirm in-game — **DONE (2026-07-17, user-confirmed)**
Character self-occlusion + black-hole occlusion fixed by §7.1, verified in the running game.
The depth-sampling degradation it noted (hard-cut soft particles) surfaced in-game and is
fixed by the §7.10 shadow-copy twin (`soft_depth` scenario) — **user-confirmed in-game: the
arena glows now fade softly into the background instead of clipping.**

### 8.2 Graphics-stage SSBO — **DONE (2026-07-16, `ssbo_vs` 11/11, zero validation errors)**
Implemented: set0 bindings 18–21 = STORAGE_BUFFER (VS|FS); new SPIR-V pass
`rlvkRebaseStorageBuffers` (rlvk_shaderc.inl) rewrites GLSL std430 bindings 0..3 → 18..21
(SSBO vars = StorageBuffer class OR Uniform+BufferBlock, shaderc's SPIR-V 1.3 shape) and
injects NonWritable when the device lacks `vertexPipelineStoresAndAtomics`
(`Caps.graphicsSsboStores`; feature enabled when present — so weak 1.1 devices get
read-only graphics SSBOs, which is all particles need); `rlvkBindShaderSsbos` pushes from
the shared `rlBindShaderBuffer` table (indices 0..3) with `pushedSsbo[]` dedup on the
native path, while `rlvkFlushSet0` writes all 4 bindings on the pool-ring path
(`rlBindShaderBuffer` marks set0Dirty). Dummy attrib buffer (STORAGE usage) backs unbound
slots. Remaining: confirm GPU particles in the actual game (human-run build).

### 8.3 ~~Port `compute/gpu_particle_system.c` from raw GL~~ — STALE, nothing to do
Checked 2026-07-16: the file is already pure rl* API (rlLoadShaderBuffer /
rlBindShaderBuffer / rlComputeShaderDispatch / rlDrawVertexArrayInstanced). With §8.2 done
the whole GPU-particle path should light up under Vulkan as-is.

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
- **Shadow-copy twin bandwidth (§7.10)**: under `Caps.noSampledDepth` the depth→buffer→R32F
  bounce runs at EVERY `rlDisableFramebuffer` of a depth-bearing FBO, every frame — even for
  depth targets that are never sampled (main depth, most shadowmaps). Correctness-first;
  future optimization = only allocate the twin + bounce for depth textures actually bound as
  a sampler (needs a "was sampled" flag or lazy first-sample creation). Quirk drivers only.
- Open validation noise: **`01211` ×30 FIXED (2026-07-17, §7.9) → suite now 0**. Only the
  known §7.5 stride-0 `04457` ×3 remains (intentional portability workaround). Verified with
  `VALIDATE=1 ./scripts/run_rlvk_visual_test.sh` (12/12, `grep -c 01211` == 0).

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
