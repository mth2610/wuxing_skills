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
  SSBO reads (`core/docs/PROGRESS.md` Item 5). Vulkan *mandates* vertex-stage storage-buffer reads
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
- **Visual test suite: 13/13 (soft_ground added)** (`scripts/run_rlvk_visual_test.sh`, see §5).
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
| `third_party/vulkan/tests/rlvk_visual_test.c` | Windowed scenario suite (13 scenarios, self-checking pixels). **Every draw-path bug fix adds a scenario here first.** |
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
- **"`rlNormal3f` does not deliver per-vertex normals through the immediate-mode batch"
  is a FALSE ALARM** (volume-tube `|N·V|` inversion class, 06/08/2026). The attributes
  ARRIVE — but they arrive *view-transformed*, and that is the point: `main.c`'s
  `MyBeginMode3D` calls `rlPushMatrix()` in RL_MODELVIEW, which arms `transformRequired`
  and parks the VIEW matrix in `State.transform`, so `rlVertex3f`/`rlNormal3f`
  CPU-transform every vertex into view space (rlgl.h:1529/1612), and the batch flush then
  uploads `matModel = State.transform` = the same view matrix (rlvk_core.inl:595). A
  shader that does `matModel * vertexNormal` on this draw path applies the view rotation
  a **second** time — which is the whole defect. The session's constant-normal probe read
  back "many colours" not because the attribute was missing but because its debug view
  (mode 5) sits above the `discard` and composites BOTH tube walls with no depth sort, so
  a pixel's colour is raster-order — camera-angle — dependent even for a constant normal.
  The core fix (`trail_volume.vs` passes attributes through; frag stage uses
  `normalize(-fragPosition)`) is guarded by the `imm_normal` scenario, which sends a
  KNOWN normal down this exact path and reads it back: raw = `view*N` (d 0.002),
  `matModel*` = `view*view*N` (d 0.005). Do not re-chase this as a backend bug.

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

### 7.26 Real-shadow ground receiver blank after a prior 3D draw — an `rlPushMatrix` transform leak (NOT a descriptor bug)
- **Symptom**: the P6 real ground shadow (immediate-mode receiver: custom-UBO shader, samples the
  depth→R32F copy at `texture0`) rendered **no shadow** whenever any ordinary 3D geometry was drawn
  earlier in the main pass. Alone in an empty pass it worked. Repro: `run_rlvk_visual_test.sh
  shadow_pipeline` (two pollution `DrawCube`s → FAIL; delete them → PASS).
- **What it looked like** (and the wrong theory it produced): the receiver sampled the **default
  white texture** (1.0 ⇒ "no occluder"). Session-5 notes concluded *"MoltenVK drops the second
  push-descriptor to binding 0 in a render pass."* **That diagnosis is wrong.** Proven wrong by
  bisection: (a) the pushed descriptor is byte-identical with/without the prior draw — copyRT view,
  `SHADER_READ_ONLY`, no substitution (probe in `rlvkPushTexture`); (b) the **pool-ring bound-set
  path fails identically** (a completely different binding mechanism — so it isn't the push path);
  (c) coalescing the whole set-0 into one push, reordering vs the pipeline bind, and an
  ALL_COMMANDS barrier before the draw all fail. The texture binding was never the problem.
- **The real bisection**: swapping the pollution draw type flipped the result. `DrawBillboard`
  (any texture, even the default) → PASS; a hand-written textured `rlBegin(RL_TRIANGLES)` → PASS;
  **`DrawCube` → FAIL**. Adding `rlPushMatrix()/rlTranslatef()/rlPopMatrix()` around the passing
  manual draw made it FAIL. So the trigger is a **MODELVIEW push/pop in a prior draw** — exactly
  what `DrawCube` does internally — not the texture, format, draw mode, or normals. A `fwp`/uniform
  probe then showed the receiver's **`u_lightVP` projection** was off (shadow landing in the clear
  region), i.e. corrupted uniform state, not a wrong texture.
- **Root cause**: `rlPopMatrix` reset `transformRequired`/`currentMatrix` gated on the **shared**
  `stackCounter == 0`. `BeginMode3D` leaves a PROJECTION `rlPushMatrix` outstanding, so a balanced
  MODELVIEW push/pop inside it never brings `stackCounter` to 0 → the reset is skipped →
  `transformRequired` stays `true` and `currentMatrix` stays `&State.transform` after the draw. The
  next custom-UBO batch flush then mis-delivered its uniforms (`u_lightVP`). (rlgl leaves
  `transformRequired` set too, but its uniform path tolerates it; rlvk's does not.)
- **Fix** (`rlvk_matrix.inl` + `rlvk_state.inl`): track MODELVIEW push depth on its own counter
  `State.mvStackDepth` (inc in `rlPushMatrix` when MODELVIEW, dec in `rlPopMatrix` when MODELVIEW);
  reset `transformRequired`/`currentMatrix` when **it** hits 0, independent of the shared
  `stackCounter`. Two-line-class change, no descriptor/MoltenVK path touched.
- **Guard**: `shadow_pipeline` scenario (the two `DrawCube`s are the trigger). `shadow_proj` +
  `shadow_cast` remain the algorithm guards. Full suite 17/17, `VALIDATE=1` clean.
- **Method note**: four sessions chased this as a texture/descriptor/MoltenVK bug. It was a CPU
  matrix-state leak. The lesson: when *two independent binding mechanisms* fail identically, stop
  blaming the binding — probe what the shader actually *computes* (here `u_lightVP`), and bisect the
  **prior** draw's side effects, not just the failing draw.

### 7.28 VFX "cut into rectangles and shuffled" — a UBO push SKIPPED on a full arena (2026-07-22)
- **Symptom** (reported in-game after P6 real shading landed): `vc_smoke_column` renders correctly
  most of the time, then intermittently comes out as **small rectangles in scrambled positions /
  wrong colors**. Worse with **several columns at once** or while **rotating the camera**; a frame
  later it can look fine again.
- **Why that VFX**: it changes `u_progress` **per column**, and a uniform change between instances
  forces `rlDrawRenderBatchActive()` — i.e. **one batch flush, and one UBO snapshot, per column**.
  It is the heaviest per-draw-uniform pattern in the engine, so it hits the arena limit first.
- **Root cause**: `rlvkAppendUboWrites` (`rlvk_shaderc.inl`) bump-allocates the shader's UBO block
  out of the per-frame arena. When the block does not fit it **returns without pushing** — the
  comment ("cannot drain here, this draw's binds would be lost") is true, but the consequence was
  never harmless: the draw keeps the **previously pushed** descriptor, i.e. **stale `mvp` + stale
  user uniforms**. Stale `mvp` = quads drawn with another draw's transform (the "shuffled
  rectangles"); stale uniforms = another draw's color/progress. And because the *vertex* payload of
  a flush is much smaller than a UBO block, once the arena has less than one block left **every**
  following flush skips until the vertex path finally drains — dozens of stale draws in a row, not
  one. P6 raised the per-frame UBO demand enough to reach that window regularly.
- **Fix**: reserve the block where draining is still legal, so the skip path is unreachable.
  `rlDrawRenderBatch` (`rlvk_core.inl`) adds `uboBytes` to the arena capacity test that already
  drives its mid-frame drain loop; `rlvkDrawMesh` (`rlvk_texture.inl`) reserves (and drains, before
  it records anything, invalidating `s_bindingValid`) at the top of the draw. The skip path in
  `rlvkAppendUboWrites` now also **TRACELOGs once** — it must never be silent again.
- **Guard**: `run_rlvk_visual_test.sh ubo_arena`. Additive accumulation makes a single stale draw
  permanent (last-draw-wins pixel checks pass by luck), and a fat zero-filled `uPad[512]` uniform
  widens the window deterministically. Before the fix: `stripe 0 is (255,151,151)`. After: PASS,
  suite 18/18.
- **Method note**: the first version of this scenario PASSED while the warning proved the skip was
  happening — a test that exercises a path is not a test that *detects* its damage.

### 7.29 §7.27 closed: the depth twin is only bounced once something samples it (2026-07-22)
- **Symptom**: the game sits at exactly 30 FPS with real shadows on, and successive optimization
  passes change nothing.
- **First, the measurement trap**: FIFO present + `SetTargetFPS(60)` **quantizes** the number. A
  17 ms frame and a 33 ms frame both report 30 FPS, so every partial win reads as "no effect" until
  the last one crosses 16.6 ms. Nothing can be tuned through an FPS counter here. Added
  `UNCAPPED=1` to `run_rlvk_visual_test.sh` (compiles the raylib cache with `PERFORMANCE_CAPTURE`
  ⇒ IMMEDIATE present, separate cache dir) and `-DWUXING_PERF_CAPTURE=ON` for the game.
- **Measured** (`perf_base` / `perf_rt256` / `perf_rt2048`, one 3D cube each, MoltenVK/Intel):
  4.75 / 6.51 / **13.42** ms/frame. The RT pass costs +1.8 ms at 256² and +8.7 ms at 2048² — it
  **scales with pixels**. That kills §7.27's recorded signature ("unchanged by resolution", which
  was asserted, never measured) and confirms the per-pixel `depth → sampleScratch → R32F twin`
  bounce as the cost. `RLVK_GPU_TRACE` is useless here: MoltenVK returns all-zero timestamps.
- **Fix**: `rlvkTextureSlot.sampleWanted`, sticky, latched in `rlvkResolveTexBinding` the first time
  anything binds the twin as a shader resource. In `rlDisableFramebuffer`, a twin that was never
  wanted emits **no barriers and no copies** — the depth image simply stays in its resting
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` and the twin keeps `sampleLayout = UNDEFINED`, which is
  exactly the `oldLayout` the bounce path itself declares for the twin ("prior contents are stale;
  discard"). **Layout bookkeeping is therefore identical either way** — that is what separates this
  from the *lazy bounce* attempt recorded under §7.27, which desynced it and reintroduced the §7.9
  `VUID-…-oldLayout-01211` class. A later first bind reads the existing default-texture
  substitution (white = far = "no occluder") for one frame, then bounces for real from then on.
- **Result**: 2048² RT pass **13.42 → 8.94 ms/frame**. Suite 18/18; `VALIDATE=1` on
  `soft_depth`/`soft_ground`/`depth_rt` = 0 / 1 (the unrelated `vertexAttributeAccessBeyondStride`
  portability VUID) / 0 — the documented baseline, unchanged.
- **In the game**: `environment/env_shadow.c`'s 2048² capture FBO is exactly the never-sampled case
  (it samples its own R32F **color** attachment; the depth attachment is only depth-tested), so it
  pays zero bounce now. Its source comment still describes §7.27 as OPEN — Environment Agent's file
  to update.
- **Guard**: the `perf_*` probes are skipped in a full run (measurement, not assertion); ask for
  them by name with `UNCAPPED=1`.

### 7.32 FBO-to-FBO scope switch left outgoing colour unreadable (2026-08-01)

**Symptom.** A scene rendered normally for its first frame, then its VFX compositor
sampled white or black after switching through a transparent layer. Every screen was
affected, including screens without visible VFX.

**Root cause.** `rlEnableFramebuffer()` ended the prior Vulkan render pass but did not
transition its colour attachments from `COLOR_ATTACHMENT_OPTIMAL` to
`SHADER_READ_ONLY_OPTIMAL`. `rlDisableFramebuffer()` performed that transition only when
returning to the swapchain, so FBO-to-FBO composition sampled an image in the wrong layout.

**Fix.** Close the outgoing user-FBO colours with the same write→fragment-read barrier
before opening the next FBO. Shared depth remains in its depth-attachment layout because
the next FBO uses it for depth testing.

**Guard.** `run_rlvk_visual_test.sh fbo_switch`: clear a layer red, switch back to the
scene FBO, then sample the layer. The output must remain strongly red-dominant on every
frame; do not require literal `(255,0,0)` because macOS screen capture is colour-managed.

### 7.31 VFX colour-layer clear erased shared scene depth (2026-08-01)

**Symptom.** A separate VFX target that shares the scene render texture's depth buffer
lost occlusion after clearing its colour to transparent. The bug looked like a VFX
blend/compositor problem because particles behind geometry appeared through it.

**Root cause.** `rlClearScreenBuffers()` unconditionally included a depth clear whenever
the active framebuffer had depth. Its caller correctly disabled depth writes around
`ClearBackground(BLANK)`, but rlvk ignored `RLVK.State.depthWrite`. OpenGL clear semantics
honour active buffer write masks.

**Fix.** Only append the depth attachment to Vulkan's `vkCmdClearAttachments` list when
`RLVK.State.depthWrite` is true. Colour attachment clears remain unaffected.

**Guard.** `run_rlvk_visual_test.sh depth_mask_clear`: write a near cube depth, clear a
render texture with depth writes disabled, then draw a far additive wall. The wall must
remain occluded at the centre.

### 7.30 Soft particles stayed hard-cut: a second sampler moved `texture0` (2026-08-01)
- **Symptom**: particles that intersect the ground retained a sharp clipped rail instead of fading.
  The game had consequently disabled its `u_cameraDepthTex` sampler: merely declaring it made the
  particle sprite render as a flat square.
- **Root cause**: shaderc auto-binding may assign the sampler named `texture0` to a descriptor
  binding other than zero when another sampler is present. rlvk's batch path nevertheless pushed
  the draw-call texture to binding 0, and its mesh path treated binding 0 as the diffuse fallback.
  The shader therefore read the depth texture/default texture where it expected the sprite.
- **Fix**: resolve raylib's draw-call texture by the reflected sampler name `texture0`, not a
  presumed binding number, in both batch and mesh sampler paths. Other reflected samplers retain
  their own explicit texture/unit binding.
- **Guard**: `run_rlvk_visual_test.sh sampler_pair` draws with a red `texture0` plus a separately
  bound green `u_cameraDepthTex` and requires both channels at the output.

### 7.33 Runtime teardown leaked linked compute programs (2026-08-16)

**Symptom.** `run_rlvk_runtime_test.sh` completed all functional checks, but the validation
layer emitted `VUID-vkDestroyDevice-device-05137` for two shader modules and two pipelines
at `rlglClose()`.

**Root cause.** `rlUnloadShader()` deliberately preserves a linked compute program when called
on its stage handle, matching GL semantics. The runtime harness intentionally exercises that
path, but `rlglClose()` only released `vertMod` and `fragMod`; it omitted each live slot's
`compMod` and `computePipeline` (`rlvk_core.inl`).

**Fix.** Shutdown now destroys both compute objects after its device-idle drain, alongside the
existing graphics shader cleanup in `rlvk_core.inl`.

**Guard.** `run_rlvk_runtime_test.sh` compiles and dispatches both compute-program forms, calls
the stage-handle unload path, then closes the backend. The validated run must finish without
`VUID-vkDestroyDevice-device-05137`.

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

### 8.4 Android (platform glue LANDED 2026-07-17, unverified on NDK/device — see below)

**Starting point**: today's shipped `android.wuxing_skills` build is **100% GL/GLES** —
`ANDROID_NOTICES.md` confirmed no Vulkan wiring ran there before this session
(`Makefile.Android`'s `USE_VULKAN=1` branch only compiled `core/vulkan/wuxing_vulkan.c`, a
no-op stub). Don't re-litigate "why doesn't Vulkan work on Android" pre-2026-07-17 — it was
never wired, not broken. Default build (`USE_VULKAN` unset/0) is **unchanged** by everything
below — dry-run-verified (`make -f Makefile.Android -n`) to emit identical flags/recipe.

**Landed this session** (all written from first principles, cross-checked against the real
`build/_deps/raylib-src` — human granted a one-off read exception for that normally-forbidden
tree, specifically to find exact patch anchors):

- `rlvkDetachSurface(void)` — `rlvk_platform.inl`/`rlvk.h`. Tears down the swapchain +
  `VkSurfaceKHR` (device-wait-idle, `rlvkDestroySwapchainSizedObjects`, `vkDestroySurfaceKHR`,
  clears `RLVK.surface`/`frameActive`). Compile-checked + full visual suite green (13/13).
- **Android raylib patch** — new section in `scripts/rlvk_patch_raylib.py` targeting
  `src/platforms/rcore_android.c` (idempotent, marker-guarded, same pattern as the existing
  GLFW-desktop section; `#else` branches are byte-identical to the original — the GL path is
  provably untouched when `GRAPHICS_API_VULKAN` is undefined). Anchor strings verified to
  match the real checked-out file via a scratch-copy dry run (`python3
  scripts/rlvk_patch_raylib.py <scratch-copy>` → "patched", brace-balance confirmed). Three
  pieces:
  - `SwapScreenBuffer` → `rlvkPresent()` + a new `WindowAttachVulkanSurface()` (mirrors the
    GLFW one, using `vkCreateAndroidSurfaceKHR` + `platform.app->window`).
  - **`APP_CMD_INIT_WINDOW` restructured around a real landmine**: Android's un-patched
    handler calls `rlglInit()` itself (plus `SetupViewport`/`InitTimer`/`LoadFontDefault`/
    `SetRandomSeed`) *inside* the callback, and then `rcore.c`'s generic `InitWindow()` calls
    `rlglInit()` **again** right after `InitPlatform()` returns — a double-init GL silently
    tolerates but Vulkan cannot (`rlglInit` creates the `VkInstance`/device; a second call
    would recreate them over the live `RLVK` globals mid-use). Fix: under Vulkan, the
    first-launch branch is reduced to just `CORE.Window.ready = true` + dimension bookkeeping;
    the one real `rlglInit` + surface attach happens from the generic `rcore.c` site via
    `WindowAttachVulkanSurface`. The resume branch (`contextRebindRequired`, i.e. after a
    prior pause) calls `vkCreateAndroidSurfaceKHR` + the already-re-entrant
    `rlvkAttachSurface` directly — no `rlglInit` involved, matching that it must never re-run.
  - `APP_CMD_TERM_WINDOW` → `rlvkDetachSurface()` in place of the EGL teardown, still setting
    `contextRebindRequired = true` (reused unchanged as the "window was lost, come back" flag
    for both paths).
- **`Makefile.Android`**: `USE_VULKAN=1` now also defines `GRAPHICS_API_VULKAN` (the macro the
  patch's `#if` guards actually check — `WUXING_USE_VULKAN` alone never activated the raylib
  source patch, a real gap in the flag as it existed before this session) and
  `VK_USE_PLATFORM_ANDROID_KHR` (must come from a compile flag — `vulkan.h` is first included,
  transitively, before `rcore_android.c` is reached in the same translation unit, so a
  `#define` inside the patch would be too late). Added: `-Ithird_party/vulkan`, `-lvulkan`
  (NDK stub, resolves the device's real `libvulkan.so` at load time, API 24+), and
  `compile_raylib_android` now (a) runs the patch script before the raylib CMake build and
  (b) passes `-DCMAKE_C_FLAGS="-DGRAPHICS_API_VULKAN -DVK_USE_PLATFORM_ANDROID_KHR
  -Ithird_party/vulkan"` into that **separate** raylib-only CMake invocation — raylib.a is
  built independently of the game's own `$(CFLAGS)`, so without this the archive would
  silently contain the GL rcore.c path regardless of the patch or the game code's own flags.
  **Landmine flagged inline**: `compile_raylib_android` skips rebuilding `libraylib.a` if the
  path already exists — flipping `USE_VULKAN` without clearing
  `android.wuxing_skills/raylib_build` + `lib/*/libraylib.a` silently reuses the stale
  wrong-backend archive (same class of bug as the two already documented in §D2 of
  `ANDROID_NOTICES.md`).
- `core/vulkan/wuxing_vulkan.c` — cosmetic-only fix (see §7.11 for the REAL bug this looked
  like it was addressing): removed `#undef GRAPHICS_API_OPENGL_33/ES2/ES3` from this no-op
  stub. Harmless either way — the file doesn't `#include` anything, so an `#undef` inside it
  has zero effect on any other translation unit's preprocessor state. Not the actual fix.

### 7.11 First real Android compile attempt — two bugs found and fixed same-day (2026-07-17)
A human ran `make -f Makefile.Android USE_VULKAN=1` for the first time against the platform
glue above. Two real bugs surfaced immediately (both fixed, neither yet re-verified — no NDK
here to confirm):
- **`GRAPHICS_API_OPENGL_33`/`ES2` simultaneously defined → `rlgl.h` duplicate-member compile
  error.** Root cause: `rlvk.h`'s forced `#define GRAPHICS_API_OPENGL_33` (needed so
  `rlVertexBuffer.indices` comes out `unsigned int*`, which rlvk's own code assumes
  unconditionally) had a `#ifndef GRAPHICS_API_OPENGL_33` guard but never checked
  `ES2`/`ES3`. Desktop never hit this (never had ES2/ES3 defined in the first place). Android
  does: raylib's own `src/CMakeLists.txt` (`cmake/CompileDefinitions.cmake`:
  `target_compile_definitions(raylib PUBLIC "${GRAPHICS}")`) unconditionally defines
  `GRAPHICS_API_OPENGL_ES3` from `-DOPENGL_VERSION="ES 3.0"`, and `rlgl.h` itself auto-implies
  `ES2` from `ES3` (a legitimate, unrelated raylib pattern — not a bug). With both `_33` and
  `_ES2` defined, `rlgl.h`'s two MUTUALLY EXCLUSIVE `#if` blocks for `rlVertexBuffer.indices`
  (32-bit for `_33`, 16-bit for `_ES2`) both compiled → duplicate struct member. **My own
  earlier "fix" to `core/vulkan/wuxing_vulkan.c` (removing its `#undef`s) was not wrong but
  was irrelevant** — that file doesn't even get included anywhere, so its preprocessor state
  never reaches `rcore.c`'s translation unit. The real fix has to live inside `rlvk.h` itself,
  which is what actually gets included into `rcore.c`. **Fix**: `rlvk.h` now explicitly
  `#undef`s `GRAPHICS_API_OPENGL_ES2`/`ES3` immediately before forcing `_33` — scoped to
  `rcore.c`'s translation unit only (where `RLVK_IMPLEMENTATION` lives), doesn't desync any
  other raylib source file (none of them read `rlVertexBuffer`'s layout directly). Desktop
  `check_rlvk_compile.sh` + full visual suite (13/13) reconfirmed green after the change.
- **`shaderc/shaderc.h` not found** compiling the Android raylib CMake build. The vendored
  copy lives at `third_party/vulkan/include/shaderc/shaderc.h` (NDK's own bundled shaderc.h
  lacks `set_vulkan_rules_relaxed` — see compute/docs/API.md), but `Makefile.Android`'s
  `-DCMAKE_C_FLAGS` for the raylib-only CMake sub-build only added `-Ithird_party/vulkan`, not
  `-Ithird_party/vulkan/include`. `scripts/check_rlvk_compile.sh` already adds BOTH (that's
  how desktop finds it without a system Vulkan SDK) — the Android Makefile just missed the
  second path. **Fix**: added `-Ithird_party/vulkan/include` to both `Makefile.Android`'s
  `INCLUDE_PATHS` (game code) and the raylib CMake sub-build's `CMAKE_C_FLAGS`.
**Second compile attempt (same day) found two more bugs — one is a real "measure twice" lesson:**
- **`VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME` undeclared.** `VK_KHR_line_rasterization` (the
  KHR promotion of the older, present `VK_EXT_line_rasterization`) is newer than the
  Vulkan-Headers bundled with NDK 28. Only the string constant is needed (queried by name via
  `vkEnumerateDeviceExtensionProperties`, never through a KHR-specific struct/enum) — **fix**:
  `rlvk_config.inl` now supplies an `#ifndef`-guarded fallback matching the exact upstream
  Khronos registry value (`"VK_KHR_line_rasterization"`). No-op on headers that already have
  it (confirmed: desktop `check_rlvk_compile.sh` still green).
- **`rcore_android.c:1072: unterminated conditional directive` → cascaded into ~20 bogus
  "function definition is not allowed here" errors.** Self-inflicted: the §7.10-era Android
  patch's `#if defined(GRAPHICS_API_VULKAN) ... #else ...` for `APP_CMD_INIT_WINDOW` never
  closed with `#endif` — the anchor was cut short at `InitTimer();` (matching where the
  *Vulkan* branch's own logic ends) instead of extending through the REST of the original,
  untouched code (`LoadFontDefault`/`SetRandomSeed`/closing braces) that the `#else` branch
  still needed to contain before an `#endif` could legally appear. The unclosed `#if` silently
  swallowed everything after it as "inside the GL branch" until the file ended, and the
  compiler's recovery from that produced the garbage cascade of "function definition" errors
  much further down — a textbook case of one root cause producing a scary-looking error wall
  (§6 point 2: sort by causality, not scariness; the FIRST error is the one that matters).
  **Fix**: extended both the anchor and the replacement to the true end of the `else { ... }`
  block, with the Vulkan branch's `#else` now correctly wrapping the complete, unmodified
  original code before its own `#endif`.
  **Verification this time was stronger than "read it carefully"**: reconstructed the
  pre-patch file from the exact original text captured earlier in this session, ran the FIXED
  patch script against it (confirmed: "patched", anchor matched byte-exact), then ran the
  result through `gcc -E -P` (preprocessor-only, no NDK headers needed) under BOTH
  `-DGRAPHICS_API_VULKAN` and without — **zero directive errors either way**, and the
  no-Vulkan output is **byte-identical** (`diff`) to preprocessing the untouched original,
  reconfirming the GL path is provably unaffected. This class of bug (preprocessor directive
  imbalance) is exactly what a plain visual code read is bad at catching and a real
  preprocessor pass catches for free — use `gcc -E -P` on any future raylib-patch edit here,
  not just eyeballing the diff.
- **CRITICAL — the live checkout is currently broken and needs a human action**:
  `build/_deps/raylib-src/src/platforms/rcore_android.c` was already patched with the FIRST
  (broken, unclosed-`#if`) version of this patch during the failed attempt above (`make`'s
  `compile_raylib_android` runs the patch script before the CMake build). Because the patch
  script's marker guard (`if MARKER in src: return`) makes every `patch()` call a no-op once
  the marker string is present, **simply re-running `make` will NOT re-apply the now-fixed
  script to that file** — it will see the marker and skip it, leaving the broken version in
  place. **The human must delete and let it re-fetch before the next attempt**:
  `rm -rf build/_deps/raylib-src` (plus the usual `android.wuxing_skills/raylib_build` +
  `lib/*/libraylib.a` cache clear) so CMake re-fetches a pristine raylib 6.0 tree and the
  now-fixed `rlvk_patch_raylib.py` applies cleanly to it.
- **Lesson**: the first two Android compile attempts found FOUR bugs total, none visible from
  source review alone — two were cross-translation-unit/cross-build-system interactions, one
  was a bundled-header version gap, one was a directive-balance mistake in my own patch that a
  preprocessor pass (not just re-reading the diff) actually catches. Exactly the §6
  methodology point: reading tells you intent, only execution (or in this case, actually
  running the preprocessor) tells you what's real. Expect more on the next attempt — normal
  for a from-scratch platform bring-up.

### 7.12 FIRST BUILD RUNS ON REAL HARDWARE (2026-07-17) — swapchain rotated/mispresented
**Milestone**: after fixing §7.11's two bugs, `make -f Makefile.Android USE_VULKAN=1` compiled
clean and the APK ran on a real device (Samsung, Mali-G68, driver reports Vulkan API 1.1.177).
logcat confirms: RLVK device init, swapchain created (2320x1080, 5 images), shaders/textures/
models all loaded, no crash, no device-lost. This is the actual Mali/Android hardware target
the whole rlvk effort was motivated by (§1) — the first time any of it has run there.
- **Symptom**: main menu renders sideways/garbled (text and buttons rotated ~90° from
  expected), and touches don't land on the visible buttons.
- **Root cause**: `rlvkAttachSurface` (`rlvk_platform.inl`) set the swapchain's
  `preTransform = caps.currentTransform` unconditionally, but rlvk's rendering never actually
  ROTATES its content to match a non-identity transform. On a phone whose native display panel
  is portrait while the app runs landscape (the common case), `caps.currentTransform` reports
  `ROTATE_90`/`270` — telling the presentation engine "this image is already pre-rotated to
  match the panel," which was false, so the compositor mis-presented the whole frame. Touch
  input arrives in the correct physical orientation regardless, so it lands on the wrong
  on-screen location relative to what's drawn (explaining "can't tap the buttons" as the SAME
  root cause, not a separate input bug). Desktop never surfaced this: window surfaces there
  report `currentTransform = IDENTITY`, so the old code was accidentally correct there.
- **Fix**: request `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` explicitly when the driver's
  `supportedTransforms` includes it (essentially universal — Android's compositor efficiently
  handles the panel-vs-app-orientation difference itself, the standard approach used by most
  engines rather than the more complex "actually pre-rotate your own draw content" path).
  Falls back to the old `caps.currentTransform` behavior on the rare device that doesn't
  support IDENTITY (no regression there). `rlvkRecreateSwapchain` reuses `rlvkAttachSurface`
  internally, so orientation-change/resize recreation picks up the same fix automatically.
  Desktop `check_rlvk_compile.sh` + full visual suite (13/13) reconfirmed green — no-op there
  as expected (MoltenVK/GLFW surfaces already report IDENTITY).
- **Not yet re-verified on device** — reasoned from the logcat + screenshot the human provided,
  not from running it myself (no device access). Also visible in this run's logcat, a SEPARATE
  known/already-tracked gap (not touched by this fix): every custom shader load logs `RLVK:
  custom shaders need shaderc_shared.dll (not found) - using default shader`, and GPU compute
  particles fell back to the CPU/VBO path for the same reason — this is exactly the
  "shaderc on device" item already listed as open below, now confirmed for real (previously
  theoretical). It means today's on-device visuals use raylib's default shader everywhere a
  skill expects a custom one, independent of the rotation bug — expect that to still look
  wrong (colors/effects) even after the rotation fix, until shaderc is addressed.

**Human retest (same day): text mirroring gone, but rotation and tap-misalignment persist.**
The IDENTITY-preTransform fix changed something (no longer mirrored) but did not fully fix the
symptom — meaning the diagnosis above was incomplete or there's a second, compounding cause.
Rather than guess again blindly, added `RLVK_DEBUG_ROTATE` (env-gated TRACELOG right where
`preTransform` is computed in `rlvkAttachSurface`) printing the raw
`currentTransform`/`supportedTransforms`/chosen-`preTransform` bits and both extents
(`caps.currentExtent` vs the swapchain's actual `extent`) — per §6 point 1, reproduce/measure
before reading further, don't keep guessing from source alone. **Landmine hit writing this
probe**: first attempt also logged `CORE.Window.render/screen` dimensions for cross-reference,
which failed to compile with "use of undeclared identifier 'CORE'" — `rlvk.h` is textually
included into `rcore.c` at the `#define RLVK_IMPLEMENTATION` / `#include "rlvk.h"` line, which
comes BEFORE `CoreData CORE = { 0 };`'s own declaration further down the same file; nothing in
`rlvk.h`/its fragments can reference `CORE`. **Also confirms `check_rlvk_compile.sh`'s
standalone harness does NOT reproduce this class of error** (it passed clean while the real
`run_rlvk_visual_test.sh` build — which compiles through the actual patched `rcore.c`, same as
the game — caught it immediately); for anything referencing raylib-side globals or depending on
real inclusion order, the visual-suite build is the one that matters, not just the compile
check. Dropped the `CORE` fields from the log; `currentTransform`/`supportedTransforms`/extents
alone are enough to diagnose the rotation. Made the log **unconditional** (not env-var-gated) —
it only fires at swapchain create/recreate (rare) and env vars don't reliably reach a
NativeActivity process the way a desktop shell launch does; marked TEMP/REMOVE-once-fixed in
the code comment. Compile-check + full visual suite (13/13) reconfirmed clean with the
corrected log. **Next step**: human rebuilds and pastes the `VKROTATE ...` logcat line (fires
automatically, no setup needed) so the actual transform/extent values replace guessing.

**Human retest with `RLVK_DEBUG_ROTATE` output (same day): `currentTransform=0x2` (ROTATE_90),
`supportedTransforms=0x1ff`, `chosenPreTransform=0x1` (IDENTITY, correctly selected),
`currentExtent`==`chosenExtent`==2320x1080.** Confirmed the ROTATE_90 hypothesis (panel truly
is native-portrait) and confirmed the IDENTITY fix WAS applied. Direct verification via `adb`
(device was connected, so used it instead of asking for more screenshots — `adb shell
screencap` + `adb shell input tap`) showed the MENU screen renders correctly oriented and taps
land on the right button (confirmed by a deliberate off-by-one-row tap correctly triggering the
adjacent button's own distinct behavior, not a random/dead miss) — the swapchain rotation fix
is real and working. **But then**: human tested in-game (entered via a stray tap, screenshot
showed a garbled/noise-texture scene with what looked like UI/health-bar elements) and reported
taps still don't land where expected, needing to "tap wildly outside" the visible target to
trigger anything — a SEPARATE, genuine bug, not a residual rotation issue.

### 7.13 Touch coordinates misaligned independent of rendering (2026-07-17) — SetupFramebuffer
never ran under the Vulkan patch
- **Root cause**: raylib's Android touch handler (`AndroidInputCallback`) scales every raw
  touch position by `widthRatio = (CORE.Window.screen.width + CORE.Window.renderOffset.x) /
  CORE.Window.display.width` (and the Y equivalent) — entirely dependent on
  `CORE.Window.renderOffset`, which is computed ONLY by `SetupFramebuffer()` (pure
  `CORE.Window.screen`/`display` math, zero EGL/GL calls, confirmed by reading its full body).
  §7.11/7.12's Vulkan branch of `APP_CMD_INIT_WINDOW` skips `InitGraphicsDevice()` entirely
  (correctly, to avoid its EGL context/surface creation) — but that also skipped the
  `SetupFramebuffer()` call bundled inside it, so `CORE.Window.renderOffset` silently stayed at
  its zero-init default. That default is only correct by coincidence when `screen` and
  `display` share the same aspect ratio; on a real device (`screen` from `InitWindow`, e.g.
  1280x720 = 16:9, vs `display` from the actual `ANativeWindow`, e.g. 2320x1080 ≈ 2.15:1 —
  different aspect ratios, hits `SetupFramebuffer`'s "upscaling, needs letterbox offset"
  branch) the missing offset put every touch at a systematically wrong position, while
  rendering looked completely correct (the swapchain extent comes from `caps.currentExtent`
  directly, entirely independent of `CORE.Window.render`/`renderOffset`) — exactly matching
  "visuals are right, taps are wrong," a genuinely different bug class from §7.12's rotation
  issue, just discovered back-to-back on the same device.
  **Bonus finding**: this also explains the earlier `rlglInit(..., 1280x720)` log line
  (§ milestone in 7.12) — that was `CORE.Window.render`'s zero-init-triggered fallback from
  `rcore.c`'s generic "embedded platforms" 0x0 guard (`CORE.Window.render` never got set to
  anything real), not an actual computed value; it happened to read 1280x720 only because that
  matches `CORE.Window.screen` (the fallback sets `render = screen` verbatim when `render` is
  still 0x0), not because any real display-aware sizing occurred.
- **Fix**: added a call to `SetupFramebuffer(CORE.Window.display.width,
  CORE.Window.display.height)` in the Vulkan branch's first-launch case, immediately after
  `CORE.Window.display.width/height` are set from `ANativeWindow_get{Width,Height}` — plus the
  same `CORE.Window.render = CORE.Window.screen` / `currentFbo` override the GL `else`-branch
  does right after a successful attach, so behavior matches the known-working GL reference
  exactly. This runs (inside `AndroidCommandCallback`, during `InitPlatform()`'s wait loop)
  BEFORE the generic `rcore.c` site's `rlglInit(CORE.Window.render.width, ...)` call (which
  only fires after `InitPlatform()` returns), so `rlglInit` will now receive the real computed
  render size instead of the screen-dimension fallback.
- **Verification**: re-fetched a fully pristine raylib checkout (`rm -rf
  build/_deps/raylib-{src,subbuild,build}` + `cmake -S . -B build`, same as §7.11's protocol —
  the marker-guard means editing the patch script alone does nothing to an already-patched
  tree) and confirmed the updated patch applied clean to that fresh checkout: `#if`/`#endif`
  balance 9/9, `gcc -E -P` reports zero directive errors. **Not yet verified on device** — the
  actual behavioral fix (touches landing correctly) can only be confirmed by a human rebuild +
  retest; nothing here proves the FORMULA reasoning is complete (there could be additional
  contributing factors not yet surfaced, same as rotation took two rounds).
- **Process note**: this is now the SECOND real Vulkan-vs-GL-Android-init-path bug from the
  same source (§7.11's double-`rlglInit`, this one's skipped-`SetupFramebuffer`) — both because
  `InitGraphicsDevice()` bundles EGL-specific work together with EGL-independent bookkeeping
  that Vulkan still needs. Before adding any FUTURE Android-Vulkan-branch logic, check whether
  the GL code path being skipped/replaced does anything else non-EGL-specific first, rather
  than assuming "skip the whole function" is safe just because the function's NAME suggests
  it's graphics-API-specific.

**Human retest (same day): touch still broken, AND a visible regression from this fix.**
Fresh `adb`-captured screenshot of the menu (device connected, used it directly instead of
relying on more secondhand description) showed something new and worse: most of the UI
(title, buttons 1–3) had scrolled off-screen entirely, only button 5 partially visible — a
rendering regression, not just unfixed touch. **Also newly noticed in hindsight: every
screenshot since the very first Android run (menu and in-game) has shown a large black
region on the right side of the display** — previously dismissed as incidental, this turned
out to be the same bug's real signature.

### 7.14 The actual root cause: `CORE.Window.render` set to the wrong size for Vulkan's model
- **Why §7.13's fix caused a regression**: it mirrored GL's `else`-branch override
  (`CORE.Window.render = CORE.Window.screen`, e.g. 1280x720) after calling
  `SetupFramebuffer()`. That override is only correct in GL's own flow because GL performs an
  extra step §7.13 didn't replicate: after `SetupFramebuffer` computes a letterboxed render
  size, GL calls `ANativeWindow_setBuffersGeometry(app->window, render.width, render.height,
  ...)` to physically shrink the EGL surface's underlying buffer to that letterboxed size —
  then relies on the OS compositor to upscale that smaller buffer to fill the real display.
  `CORE.Window.render` gets reset to `screen` afterward because the GL viewport only ever
  needs to fill that already-shrunk buffer, not the full display. **rlvk's swapchain has no
  equivalent step**: `rlvkAttachSurface` creates it directly at `caps.currentExtent` (the
  full display resolution, 2320x1080) — nothing shrinks it first. Setting
  `CORE.Window.render = CORE.Window.screen` (1280x720) made `SetupViewport`'s
  `rlViewport(renderOffset.x/2, renderOffset.y/2, render.width, render.height)` size the
  Vulkan viewport SMALLER than the swapchain image — everything outside that sub-rectangle is
  simply never drawn to, which is exactly the black region on the right that's been in every
  screenshot since the first Android run. It also explains the touch misalignment: the
  touch-scaling formula's assumptions (`widthRatio = (screen.width+renderOffset.x)/
  display.width`) are built around GL's implicit OS-level buffer-upscale step actually
  happening; nothing here performs an equivalent step for Vulkan, so the formula's inputs
  never matched what was actually on screen.
- **Fix**: stopped mirroring GL's override entirely. `CORE.Window.render` is now set to the
  FULL display resolution (`CORE.Window.display.width/height`, i.e. what the swapchain
  actually is) with `renderOffset = (0, 0)` — no letterboxing, since Vulkan is using the true
  native resolution directly. This makes the Vulkan viewport span the entire swapchain image
  (no more black region) and makes the touch-scaling formula's per-axis ratios
  (`screen.width/display.width`, `screen.height/display.height` — non-uniform since
  `screen`=1280x720 and `display`=2320x1080 have different aspect ratios) correctly invert
  whatever non-uniform stretch raylib's 2D orthographic projection applies when it maps
  `CORE.Window.screen`-space UI coordinates onto a same-sized-as-swapchain viewport — the
  render side and the touch side now agree on the same physical space instead of two
  different, GL-shaped assumptions that Vulkan's model never actually satisfies.
- **Verification**: re-fetched pristine raylib again (same protocol) — patch applied clean,
  `#if`/`#endif` balance 9/9, zero directive errors, desktop `check_rlvk_compile.sh` + full
  visual suite (13/13) still green (this patch only touches `rcore_android.c`, so desktop is
  provably unaffected either way, but reconfirmed regardless). **Not yet verified on device.**
- **Confidence note**: this reasoning is more mechanistic (traced the actual GL code path
  being diverged from, identified the specific missing step) than §7.12/§7.13's more
  empirical trial-and-observe rounds, but it is still unverified on real hardware. If this
  doesn't fully resolve it, the next diagnostic step should be screenshots of BOTH the full
  screen (confirm no more black region) AND a deliberate tap-vs-visual-target comparison
  (tap a specific labeled button, report exactly what happens), rather than a general
  "still broken" — precise repro detail matters more than more guessing from first principles
  at this point.

**§7.14 partially confirmed same day, via direct `adb` access (device was connected — used
`adb shell screencap`/`input tap` directly instead of relying on more screenshots):** the 2D
UI layer (menu buttons, HUD bars, joystick, skill buttons) now renders correctly and fills the
FULL display — the black region on the right is completely gone for that layer, confirming
§7.14's `render=display` fix is right. But a fresh in-game screenshot showed a NEW, more
specific shape: the 3D game-world viewport itself (not the 2D UI) renders as a smaller white
rectangle confined to the upper-left, with black filling an L-shaped region around it (right
side + bottom) — visible in every screenshot since the very first Android run, previously
mis-attributed to the already-fixed black-bar bug instead of being its own separate issue.

### 7.15 `core/` HDR scene composite still blits 1:1 instead of scaling to the real render size
- **Root cause**: `core/screen_distort.c` and `core/post_fx.c` each render the 3D scene into an
  intermediate `RenderTexture2D` sized at `GetScreenWidth()/GetScreenHeight()` (raylib's
  LOGICAL window size, `CORE.Window.screen` — e.g. 1280x720, unchanged by any of §7.11–7.14's
  fixes, which deliberately only touch `CORE.Window.render`), then blit that texture onto the
  actual screen/swapchain with `DrawTextureRec` — which draws the source at its native pixel
  size with NO destination scaling. On GL (desktop, and the original Android GL build), this
  was invisibly correct because `CORE.Window.render` was ALSO always the same 1280x720 logical
  size — GL's own `ANativeWindow_setBuffersGeometry` step (see §7.14) physically shrinks the
  real window buffer to that logical size and lets the OS compositor upscale it to fill the
  real display, so a 1:1 draw really did fill the (smaller, soon-to-be-upscaled) target. rlvk's
  Vulkan swapchain has no such step (per §7.14, it's created directly at the real display
  resolution) — with `CORE.Window.render` now correctly reflecting that real size, the
  intermediate texture (still logical-size) drawn 1:1 only covers a fraction of the actual
  screen, leaving the rest black — exactly the L-shaped region observed.
- **Fix**: changed both final-composite blits from `DrawTextureRec` (implicit 1:1) to
  `DrawTexturePro` with the destination rectangle sized to `GetRenderWidth()/GetRenderHeight()`
  (raylib's existing accessor for `CORE.Window.render` — the ACTUAL render/swapchain target
  size, distinct from `GetScreenWidth/Height`'s logical size) — `core/screen_distort.c`'s
  distortion-shader final blit and `core/post_fx.c`'s bloom/tonemap "PASS 6: Composite →
  screen". This is a `core/` change (Core Agent's normal territory) made directly in this
  session since it's a direct continuation of the same bug chain — flagged clearly here in
  case `core/docs/API.md` conventions around `DrawTextureRec`/`GetScreenWidth` vs `GetRenderWidth`
  in final-blit code need a note for future skill/VFX authors.
  **Not fully audited**: `core/metaball_fx.c` has a similar-shaped `DrawTexturePro` call
  (already scale-aware, not `DrawTextureRec`) using `GetScreenWidth()/GetScreenHeight()` for
  its destination rect instead of `GetRenderWidth/Height` — left unchanged since it wasn't
  confirmed whether its target at that point is the final swapchain or an intermediate texture
  sized the same logical way (in which case it's already correct as-is); worth checking if
  metaball effects show the same L-shaped-black-region symptom after this fix lands.
- **Not yet verified on device** — reasoned from tracing the exact draw calls, not from
  running it. Human should rebuild (desktop OR Android) and confirm the 3D game view now
  fills the whole screen with no black region, then retest touch alignment now that both the
  rlvk-side (render/renderOffset) and core-side (composite blit scale) halves of this bug
  chain are addressed.

**§7.14/§7.15 confirmed same day: screen now filled (no black region), but round UI elements
render as ELLIPSES and touch is still off.** Direct visual confirmation that `render=display`
(§7.14) was itself still wrong, in a new way: `CORE.Window.screen` (1280x720, 16:9≈1.778) and
`CORE.Window.display` (2320x1080, ≈2.148) have DIFFERENT aspect ratios, and setting
`render=display` scales the two axes by different factors (1.8125x vs 1.5x) — anything drawn
as a circle in logical screen-space (joystick base, skill buttons) comes out visibly
non-uniformly stretched into an ellipse. This is hard evidence, not just reasoning: two prior
attempts (§7.13 `render=screen`, §7.14 `render=display`) were each wrong in an opposite way,
and this is the failure mode that finally makes the actual bug class legible.

### 7.16 Correct fix: uniform-scale letterbox instead of either non-letterboxed extreme
- **Root cause, precisely**: neither "render=logical size" (§7.13, viewport too small — black
  region) nor "render=physical size" (§7.14, viewport right size but WRONG aspect — ellipses)
  is correct when `screen` and `display` have different aspect ratios. The only
  shape-preserving option that also avoids leaving any part of the swapchain undrawn is a
  **uniform scale factor letterboxed to fit** — same factor on both axes (so circles stay
  circles), sized so the render rectangle fits entirely within the display (so the Vulkan
  viewport never exceeds the swapchain), centered via `renderOffset` (so any leftover space
  becomes symmetric black bars on the shorter axis, not an undrawn corner).
- **Fix**: replaced the `render=display` override with a direct uniform-scale computation:
  `scale = min(display.width/screen.width, display.height/screen.height)`,
  `render.{width,height} = round(screen.{width,height} * scale)`,
  `renderOffset.{x,y} = display.{width,height} - render.{width,height}`. (Deliberately NOT
  routed through `SetupFramebuffer()` itself — its branches assume GL's separate
  `ANativeWindow_setBuffersGeometry` + OS-compositor-upscale step exists, which rlvk's
  swapchain never performs; computing the letterbox directly, tailored to "the swapchain IS
  the real display, no OS upscale step," is simpler and avoids relitigating which of
  `SetupFramebuffer`'s branches would even apply correctly here.) With this, `SetupViewport`'s
  `rlViewport(renderOffset.x/2, renderOffset.y/2, render.width, render.height)` produces a
  centered, correctly-proportioned viewport with matching letterbox bars — and
  `core/screen_distort.c`/`core/post_fx.c`'s §7.15 blits to `GetRenderWidth/Height` now target
  that same correctly-proportioned region, and `AndroidInputCallback`'s touch formula
  (`(screen+renderOffset)/display` per axis) inverts the exact same uniform transform.
- **Verification**: re-fetched pristine raylib (same protocol), patch applied clean, `#if`/
  `#endif` balance 9/9, zero directive errors, desktop compile-check + full visual suite
  (13/13) still green. **Not yet verified on device** — third attempt at this specific
  sub-problem; if STILL wrong, get a screenshot + report whether shapes are now round (confirms
  uniform scale landed) even if position is still off (would then point at something in the
  touch-formula's OTHER input, `CORE.Window.screen`, or at the UI/button code's own
  hit-test math rather than at the render/renderOffset computation itself).

**§7.16 confirmed same day: shapes ARE round now, movement joystick works (usable, confirmed by
a spawned particle), but menu buttons still don't hit-test where they visually appear** ("nút
hiển thị để bấm ở 1 nơi, vùng bấm vào có hiệu lực ở 1 nơi khác" — button displays in one place,
its effective tap region is somewhere else). This is a strong new data point: since the
joystick (same rendering pipeline, same touch-scaling formula) DOES work, the render/touch
FOUNDATION from §7.16 is very likely correct — the remaining bug is probably specific to how
the menu buttons compute or consume their hit-test coordinates, not a global transform issue.

**Investigation via direct `adb` access** (device connected — used it instead of asking for
more screenshots): read `main.c`'s menu code directly — `btnSandbox` etc. are computed fresh
every frame from `GetScreenWidth()/GetScreenHeight()` (`sw/2 - 150`, ...) and hit-tested via
`CheckCollisionPointRec(GetMousePosition(), btnSandbox)` — internally consistent, no obvious
bug in that code by inspection alone. A precise `adb shell input tap` at the visually-measured
button center (from a fresh `adb shell screencap`) did NOT trigger navigation. Chased one
promising lead that turned out NOT to be the cause, but IS worth recording: `adb shell dumpsys
window com.mth2610.wuxing` shows the app's actual window frame is `Rect(80, 0 - 2400, 1080)` —
offset 80px from the physical panel's left edge (a camera-cutout avoidance inset;
`mOverlappingWithCutout=false` confirms Android deliberately shrank/shifted the window to dodge
it), exactly matching why `caps.currentExtent.width` (2320) is 80px less than the full physical
panel width (`adb shell wm size`: 2400 physical). This is NOT a bug — `ANativeWindow_getWidth/
Height()` (what `CORE.Window.display` uses) correctly reports the app's OWN receivable
2320-wide area, matching what the Vulkan swapchain uses; Android's input dispatch is expected
to transparently deliver window-relative touch coordinates to the app regardless of this
offset (true for both real touches and `adb shell input tap`, which inject at the same global-
display level real touches do). Recorded here so it isn't rediscovered and mistaken for the
bug in a future session — it explains the 2320-vs-2400 numbers but doesn't explain the button
mismatch.

**Diagnostic added**: a `TraceLog` in `main.c`'s menu block (TEMP, marked for removal), firing
on every click anywhere on the menu screen, printing `mousePos` (post-scaling, what
`CheckCollisionPointRec` actually tests against), `sw`/`sh` (`GetScreenWidth/Height`,
the button rects' own coordinate space), `GetRenderWidth/Height`, and `btnSandbox`'s computed
rect — the exact numbers needed to see directly whether `mousePos` is inside/outside/near
`btnSandbox` and by how much, rather than continuing to infer from screenshots and manual pixel
measurement. **Next step**: human rebuilds, taps ANYWHERE on the menu (doesn't need to land on
a button — the log fires on any click), and pastes the `MENUTAP ...` logcat line.

### 7.17 Found it: `CORE.Window.screen` never kept in sync with the letterboxed `render` size
- **The `MENUTAP` data point**: `mousePos=(303.3,440.0) sw=1280 sh=720 renderW=1920 renderH=1080`.
  `renderW/sw == renderH/sh == 1.5` exactly (zero offset at that moment) — confirms §7.16's
  letterbox math itself is correct. But `sw`/`sh` (what `main.c`'s button rects are computed in)
  are still the **original** 1280×720 `InitWindow()` request, while rendering happens in
  1920×1080 `render` space.
- **Root cause**: raylib's `SetupViewport()` (`rcore.c:4261`, stock/unpatched, shared by every
  platform) always sets the ortho projection to `rlOrtho(0, CORE.Window.render.width,
  CORE.Window.render.height, 0, ...)` — **never** `screen`. All 2D draw calls (`DrawRectangle`,
  `DrawText`, ...) are therefore always in `render`-pixel space, everywhere, on every platform.
  This has always been invisible on GL/Android specifically because stock
  `rcore_android.c`'s `APP_CMD_INIT_WINDOW` sets `CORE.Window.currentFbo = CORE.Window.screen`
  (small, the original request) — `SetupFramebuffer()`'s own bigger `render` computation for
  aspect-fit purposes is fed to `ANativeWindow_setBuffersGeometry`, which creates a *physically
  small* native buffer and lets the OS compositor upscale it to the real display at present
  time, transparent to the app. So on GL, `render == currentFbo == screen` numerically, always,
  and the whole codebase's `GetScreenWidth()`-based UI layout (menu, HUD, VFX sandbox — every
  file) has silently depended on that equality.
- rlvk's swapchain has no such small-buffer-then-OS-upscale step (§7.16) — it's created directly
  at the real display extent — so §7.16's fix correctly set `currentFbo = render` (letterboxed,
  bigger than `screen`) to make the Vulkan viewport fill the swapchain. But it left
  `CORE.Window.screen` untouched at the original small request. Net effect: 2D content draws in
  the bigger `render` space (1920×1080) while `main.c` lays out and hit-tests buttons in the
  smaller, stale `screen` space (1280×720) — buttons draw compressed into a fraction of the
  visible viewport (confirmed by screenshot: menu items confined to the top-left ~40% of the
  display, rest a plain gray clear-color fill) AND get hit-tested against coordinates that no
  longer correspond to where they're drawn. Two independent-looking symptoms, one cause.
- **Fix** (`scripts/rlvk_patch_raylib.py`, same `APP_CMD_INIT_WINDOW` Vulkan block as §7.16):
  after computing the letterboxed `render`/`renderOffset`, also set
  `CORE.Window.screen.width = CORE.Window.render.width` (and `.height` likewise) — keeping
  `screen` in lockstep with `render`, restoring the "draw and hit-test share one coordinate
  space" invariant the rest of the codebase depends on. `AndroidInputCallback`'s
  `widthRatio = (screen.width + renderOffset.x)/display.width` reads `CORE.Window.screen` live
  (not cached), so this also fixes touch mapping for free — worked through both cases by hand:
  zero-offset (`screen==render==display`) degenerates to ratio 1.0, pure passthrough; letterboxed
  (`renderOffset != 0`) degenerates to `rawTouch - renderOffset/2`, i.e. subtract the half-bar
  offset with no further scaling needed, since content is already drawn 1:1 in real render
  pixels. Absolute pixel sizes (e.g. a fixed `300×50` button) will render visually smaller
  relative to the full display than originally authored at 1280×720 — cosmetic only, tunable
  later; correctness (position + hit-test agreement) matters more right now.
- **Verification**: re-fetched pristine raylib (same protocol), patch applied clean (no
  directive changes this time — pure C statements inside an existing brace block), desktop
  compile-check + full visual suite still 13/13 green, confirming the GL `#else` branch and
  desktop path are untouched. **DEVICE-CONFIRMED (2026-07-17)**: user rebuilt, menu buttons now
  hit-test at their visual position. Both TEMP diagnostics removed (`MENUTAP` in `main.c`,
  unconditional `VKROTATE` log in `rlvk_platform.inl`) — `check_rlvk_compile.sh` still green
  after removal.

**Menu/UI touch-position bug: RESOLVED end-to-end (§7.12→§7.17).**

### 7.18 shaderc on device — statically-linked NDK shaderc (2026-07-17, desktop-verified only)
- **Problem**: every custom GLSL shader falls back to raylib's default (logcat: `RLVK: custom
  shaders need shaderc_shared.dll (not found) - using default shader`), and GPU compute
  particles fall back to their CPU/VBO path for the same reason — `rlvkLoadShaderc()`'s
  dlopen sonames list (`libshaderc_shared.so`, `libshaderc.so`, ...) never resolves on Android
  because **there is no prebuilt shaderc `.so` on-device to find**: NDK 28 ships shaderc only
  as SOURCE (`$(NDK)/sources/third_party/shaderc/`, full glslang + SPIRV-Tools + SPIRV-Headers
  tree with `Android.mk` for `ndk-build`), not a binary — confirmed by `find`, no `.so`/`.a`
  anywhere under that tree.
- **What that source tree actually builds**: `ndk-build libshaderc_combined` (the target name is
  NOT the default — must be requested explicitly) produces a single **static** archive,
  `libshaderc_combined.a` (glslang+SPIRV-Tools+SPIRV-Headers+shaderc `ar -M`-combined by Google's
  own `Android.mk`, ~20 MB for arm64-v8a), plus `libc++_shared.so` (the app built with
  `APP_STL := c++_shared`, so all the C++-implemented libraries need the shared C++ runtime at
  load time). Verified buildable in isolation this session (`ndk-build` in a throwaway scratch
  project pointed at the NDK's own `Android.mk`) — ~2 min, clean exit, `llvm-nm` confirms every
  symbol `rlvkLoadShaderc`'s core function list needs is present as `T` (defined) in the archive.
- **A static archive can't be `dlopen`'d** — the whole rest of `rlvkLoadShaderc()` assumes a
  shared lib loaded and `dlsym`'d at runtime. Android needs a structurally different path:
  **statically link `libshaderc_combined.a` into `lib$(PROJECT_LIBRARY_NAME).so` at Android
  build time** and call the shaderc C API directly (linker resolves the symbols; no
  dlopen/dlsym needed, and this path can't fail at runtime the way a missing `.so` could).
  Added a `#elif defined(__ANDROID__)` branch in `rlvkLoadShaderc()` (`rlvk_shaderc.inl`) that
  just does `p_shaderc_xxx = shaderc_xxx;` for each function — the prototypes are already
  visible from `<shaderc/shaderc.h>` (included unconditionally in `rlvk.h`), so this is a plain,
  ordinary function-pointer assignment.
- **Version gap, handled**: the NDK's bundled shaderc is old enough to be **missing
  `shaderc_compile_options_set_vulkan_rules_relaxed`** entirely (`diff` against the project's
  vendored newer `shaderc.h` — only 3 functions differ; the other two,
  `set_preserve_bindings`/`set_max_id_bound`, aren't used anywhere in this codebase). Linking
  that symbol in would be a build-time undefined-symbol error. Split
  `RLVK_SHADERC_FUNCS`/`RLVK_SHADERC_FUNCS_CORE` (the latter omits it) so the Android branch
  only wires up what actually exists; the call site (`rlvkCompileGlsl`) now null-checks the
  pointer before calling it, degrading gracefully rather than crashing. Building a genuinely
  newer shaderc from actual upstream source (needs network + glslang/SPIRV-Tools/SPIRV-Headers
  pinned to a compatible commit, cross-compiled via CMake like raylib itself) would close this
  gap properly — **not attempted this session** given the added scope/fragility (network fetch +
  submodule pinning) versus the payoff (`auto_bind_uniforms` + `auto_map_locations`, both
  present in the NDK's version, already cover most of "accept stock GL-dialect GLSL";
  `vulkan_rules_relaxed` is an incremental relaxation on top, not load-bearing for every
  shader). Left as a known, documented gap — revisit if specific shaders still fail to compile
  on Android after this lands.
- **Build wiring** (`Makefile.Android`): new `compile_shaderc_android` target (same
  cache-if-exists pattern as `compile_raylib_android` — builds once, skips on rerun, same
  staleness caveat as §D2 if NDK/toolchain versions change underneath it), builds via a
  throwaway `ndk-build` project (mirrors this session's scratch verification exactly), copies
  `libshaderc_combined.a` + `libc++_shared.so` into `$(PROJECT_BUILD_PATH)/lib/$(ANDROID_ARCH_NAME)/`.
  Wired as a `compile_project_code` prerequisite (alongside `compile_raylib_android`). Link line
  gets `-lshaderc_combined -lc++_shared` (order matters — shaderc's undefined C++ symbols are
  resolved by c++_shared, which must come after it) plus
  `-Wl,--exclude-libs,libshaderc_combined.a` (keep its huge internal symbol set out of the
  final `.so`'s dynamic symbol table). `create_project_apk_package` now also `aapt add`s
  `libc++_shared.so` into the APK — it's a genuine runtime dependency, not a build-time-only
  static lib.
- Also fixed the misleading `rlvk_shader.inl` fallback log (`"shaderc_shared.dll (not found)"`
  printed unconditionally on every platform, including Linux/Android/macOS) to platform-neutral
  wording.
- **Verification**: desktop `check_rlvk_compile.sh` green after both edits (Android branch is
  behind `#elif defined(__ANDROID__)`, never compiled/type-checked on desktop — reviewed by hand
  instead). `llvm-nm` symbol-presence check against the actual built archive (see above) is the
  closest thing to a link test available without running the full Android build.

**First real build attempt hit two issues, both fixed same day:**
1. `compile_shaderc_android`'s `libc++_shared.so` copy sourced it from `ndk-build`'s own `libs/`
   output — that only gets populated for actual shared-lib targets, not a static-archive-only
   build like this one (confirmed missing on the real build, not hypothetical). Fixed: copy it
   directly from the NDK toolchain's own sysroot instead
   (`$(ANDROID_TOOLCHAIN)/sysroot/usr/lib/$(ANDROID_SYSROOT_TRIPLE)/libc++_shared.so`, always
   present there per-ABI). Added `ANDROID_SYSROOT_TRIPLE` (`aarch64-linux-android` /
   `arm-linux-androideabi`) alongside the existing `ANDROID_ARCH_NAME` mapping. **Caveat for next
   rebuild**: the cache-check only tests for `libshaderc_combined.a` (which *did* copy
   successfully before the original failure), so a plain rerun sees it, skips the whole block,
   and still lacks `libc++_shared.so`. One-time fix: `rm -f
   android.wuxing_skills/lib/arm64-v8a/libshaderc_combined.a` before rebuilding (the `ndk-build`
   object cache under `shaderc_build/obj` survives, so the retry is fast).
2. **The real blocker, found via device logcat once shaderc started actually compiling
   shaders**: `RLVK: GLSL compile failed: rlvk:9: error: 'attribute' : Reserved word` and
   `ES shaders for SPIR-V require version 310 or higher`. Root cause: `copy_project_resources`
   unconditionally ran `scripts/convert_shaders_to_gles.py` on every shader asset — converting
   desktop-dialect GLSL 330 (`in`/`out`, `#version 330`) to GLES 100/300es dialect
   (`attribute`/`varying`, `#version 100`). Correct for the GL/GLES backend; actively wrong for
   Vulkan — shaderc targets vulkan1.1/SPIR-V1.3 and rejects GLES-100 syntax outright
   (`attribute`/`varying` are reserved words in that dialect, ES `#version` directives need 310+
   for SPIR-V). This ran for EVERY Android build regardless of `USE_VULKAN`, so it silently
   corrupted every shader asset the Vulkan build shipped. Fixed: gated the conversion behind
   `ifneq ($(USE_VULKAN), 1)` in `Makefile.Android` — skipped entirely for Vulkan builds, still
   runs for the default GL build (dry-run diffed both paths to confirm). This was almost
   certainly the actual cause of "nothing changed" screenshots even after the shaderc fix landed
   — shaderc was loading and running, just being fed shaders it structurally couldn't accept.
   The `'non-opaque uniforms outside a block'` error seen alongside these two IS the
   already-documented `vulkan_rules_relaxed` gap (a real but secondary contributor) — worth
   re-checking whether it still appears once the GLES-conversion bug is fixed, since some of it
   may have been misattributed to shaders that were actually GLES-mangled, not just missing the
   relaxed-rules knob.
- Also confirmed (unrelated to shaderc, pre-existing): the swapchain recreates every single
  frame (`RLVK: swapchain created`/`recreated` in logcat at ~60 Hz) — `vkAcquireNextImageKHR`
  returning `VK_ERROR_OUT_OF_DATE_KHR` every frame, most likely because §7.12's `preTransform`
  choice (force `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` over the device's actual
  `currentTransform=ROTATE_90`) causes the compositor to keep signaling the swapchain as
  out-of-date, and recreating with the same IDENTITY choice never resolves it. This has
  seemingly been happening since §7.12 landed, silently, without visibly blocking rendering
  (menu/joystick/movement all confirmed working on top of it in §7.13–§7.17) — wasteful but not
  (so far) correctness-breaking. Not fixed this session; flagged for follow-up since constant
  swapchain teardown/rebuild is not free and could matter more once real shaders/GPU particles
  are actually rendering through it.

### 7.19 GLES-conversion fix confirmed working; found the REAL dominant blocker
Device rebuild + fresh logcat after §7.18's two fixes confirmed the `libc++_shared.so`/
GLES-conversion patches landed correctly (huge disk-space scare along the way — the dev
machine hit 100% full mid-build, causing an earlier confusing "nothing changed" report from
a build that had actually partially failed; resolved once space was freed). Real signal: most
shaders (`decal_flow.fs`, `trail_glow.fs`, `bloom_*.fs`, `distortion.fs`, `post_process.fs`,
`metaball_threshold.fs`, ...) now compile as genuine desktop GLSL 330 — no more
`attribute`/`varying` errors. Progress confirmed.

But two more things surfaced from the same logcat:
1. **`#include`-using shaders still failed** with `ES shaders for SPIR-V require version 310
   or higher` (`effect_material`, `stone_prison`, `smoke_column`, `surface_lit`,
   `volume_smoke`, the GPU-particle compute shader). Root cause: a SEPARATE, RUNTIME
   GLES-rewrite path in `core/shader_preprocessor.c` — `RewriteVersionForGLES()`, gated only
   by `#ifdef __ANDROID__` (no Vulkan check), rewrites `#version 330` → `#version 300 es`
   after `#include` expansion for every shader that goes through
   `ShaderPreprocessor_Load()`. This is a completely different code path from
   `convert_shaders_to_gles.py` (build-time, only touches shaders WITHOUT `#include` per that
   script's own `has_include_directives()` skip) — fixing the build-time script didn't touch
   this runtime path at all. Fixed: `#if defined(__ANDROID__) && !defined(GRAPHICS_API_VULKAN)`
   on both the function definition and its call site in `core/shader_preprocessor.c`.
2. **The real dominant blocker, corrected from §7.18's too-optimistic read**: `'non-opaque
   uniforms outside a block'` appeared in essentially EVERY custom shader, `#include` or not —
   not the minor edge case §7.18 assumed (`auto_bind_uniforms`/`auto_map_locations` do NOT
   cover this case; they're about binding/location assignment, not the declaration-outside-a-
   block rule itself). This is entirely the missing `vulkan_rules_relaxed` gap, and with it
   being this pervasive, "ship without it and see if shaders still mostly work" was the wrong
   call. Investigated properly this time: the NDK's bundled shaderc source tree
   (`sources/third_party/shaderc/`) links against glslang, and **glslang itself already
   implements the underlying feature** — `glslang::TShader::setEnvInputVulkanRulesRelaxed()`
   exists in `libshaderc/third_party/glslang/glslang/Public/ShaderLang.h`, confirmed by
   grepping the actual NDK-bundled source. Only libshaderc's thin C-API wrapper
   (`shaderc_compile_options_set_vulkan_rules_relaxed`) was missing — a ~15-line, 4-file gap,
   not a whole-library version problem. Rather than fetching real upstream shaderc (network +
   glslang/SPIRV-Tools/SPIRV-Headers pinned to a compatible commit — bigger, more fragile),
   added `scripts/rlvk_patch_shaderc.py`: same idempotent marker-guarded pattern as
   `rlvk_patch_raylib.py`, patches a **staged copy** of the NDK shaderc source (never the NDK
   install itself) to add the option storage field + setter in
   `libshaderc_util/include/libshaderc_util/compiler.h`, the `shader.setEnvInputVulkanRulesRelaxed()`
   call in `libshaderc_util/src/compiler.cc` (same injection point as the existing
   `setAutoMapLocations` call), and the C API wrapper itself in `libshaderc/src/shaderc.cc` +
   its declaration in `libshaderc/include/shaderc/shaderc.h`. `Makefile.Android`'s
   `compile_shaderc_android` now does `cp -r` the NDK source into
   `$(PROJECT_BUILD_PATH)/shaderc_src`, patches that copy, and points `ndk-build` at it instead
   of the NDK's own `Android.mk`.
   `rlvk_shaderc.inl` reverted to a single `RLVK_SHADERC_FUNCS` list (the temporary
   `RLVK_SHADERC_FUNCS_CORE` split and the null-check at `rlvkCompileGlsl`'s call site are gone
   — no longer needed once the function is guaranteed present).
- **Verification**: patch reapplies cleanly against a fresh staged copy; the patched source
  builds clean via `ndk-build` in isolation (~2-3 min, same as before); `llvm-nm` confirms
  `shaderc_compile_options_set_vulkan_rules_relaxed` is now a defined (`T`) symbol in the
  built archive. Desktop `check_rlvk_compile.sh` + full 13/13 visual suite still green (Android
  branch never compiled/type-checked on desktop, as before). Makefile dry-run confirms the new
  staged-copy-then-patch flow is wired correctly. **Not yet run through the real
  `Makefile.Android` end-to-end or on device.**
- Also confirmed (unrelated to shaderc, pre-existing, still unfixed): the swapchain recreates
  every single frame (`RLVK: swapchain created`/`recreated` in logcat at ~60 Hz) —
  `vkAcquireNextImageKHR` returning `VK_ERROR_OUT_OF_DATE_KHR` every frame, most likely because
  §7.12's `preTransform` choice (force `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` over the
  device's actual `currentTransform=ROTATE_90`) causes the compositor to keep signaling the
  swapchain as out-of-date, and recreating with the same IDENTITY choice never resolves it.
  Seemingly harmless so far (menu/joystick/movement all confirmed working on top of it) but
  wasteful, and worth revisiting once real shaders are actually rendering through it.

### 7.20 vulkan_rules_relaxed patch had no effect — a second, deeper glslang gap
Two device rebuilds in a row with §7.19's shaderc patch still showed
`'non-opaque uniforms outside a block'` on every custom shader, unchanged. First suspected
the `compile_shaderc_android` cache (the archive from BEFORE the patch existed was still
sitting at `lib/arm64-v8a/libshaderc_combined.a`, since the cache-check only looks at that one
file's existence) — cleared it, forced a genuinely fresh `ndk-build` from the patched source,
same result. So the patch itself wasn't reaching the compiler; traced why with `grep` instead
of guessing further:
- `ParseHelper.cpp`'s `transparentOpaqueCheck` (the function that raises this exact error)
  checks `spvVersion.vulkan > 0 && !spvVersion.vulkanRelaxed` — confirmed `vulkanRelaxed` is
  exactly the right flag, not a red herring.
- But `TShader::setEnvInputVulkanRulesRelaxed()` (what §7.19's patch calls) only sets
  `environment.input.vulkanRulesRelaxed` — a *staging* field on the `TShader`, not the live
  `spvVersion.vulkanRelaxed` the parser actually reads.
- `ShaderLang.cpp`'s `TranslateEnvironment()` is what copies staging → live, but only inside
  `case EShClientVulkan:` of a `switch (environment->input.dialect)` — reached only if
  `environment->input.languageFamily != EShSourceNone`, which requires
  `TShader::setEnvInput()` to have been called first.
- `grep -n "setEnvInput\b" libshaderc_util/src/compiler.cc` → **zero matches**. libshaderc's
  compiler driver never calls `setEnvInput()` at all (only `setEnvClient`/`setEnvTarget`,
  which touch different fields entirely). So `environment.input.languageFamily` stays
  `EShSourceNone` forever, the whole dialect-switch branch never runs, and the relaxed-rules
  flag silently never reaches `spvVersion.vulkanRelaxed` — regardless of §7.19's patch being
  100% correctly wired on the `TShader` side. A genuine second gap, not a mistake in the first
  patch.
- **Fix**: added a 5th patch (`rlvk_patch_shaderc.py`) to `glslang/MachineIndependent/
  ShaderLang.cpp`'s `TranslateEnvironment()` — apply `environment->input.vulkanRulesRelaxed`
  to `spvVersion.vulkanRelaxed` unconditionally, right next to where `spvVersion.vulkan` itself
  already gets set from the `EShMsgVulkanRules` messages flag (the path libshaderc actually
  exercises), instead of leaving it gated behind the dialect-switch branch libshaderc never
  reaches.
- **Verification**: patch applies cleanly against a fresh staged copy (5/5 files); `ndk-build`
  of the doubly-patched source completes clean, `libshaderc_combined.a` produced. Root cause
  traced via `grep` across `ParseHelper.cpp`/`ShaderLang.cpp`/`compiler.cc` with each claim
  checked against the actual NDK-bundled source, not assumed. **Not yet verified on device** —
  functional confirmation (does the shader actually compile now) requires a human rebuild.

**Still open / explicitly NOT done**:
- **Rebuild + retest** with all of §7.18's, §7.19's, and §7.20's fixes (`libc++_shared.so`
  sysroot copy, GLES-conversion build-time skip, `RewriteVersionForGLES` Vulkan guard, and NOW
  the two-part shaderc patch — the C-API wrapper AND the `TranslateEnvironment` fix, both
  needed together) — none of this has been functionally verified on device yet, only reasoned
  from logcat + isolated `ndk-build` testing.
  **Also free disk space first** — the dev machine was at 829Mi free during §7.19's session,
  which caused at least one confusing false "nothing changed" report from a build that had
  silently failed partway; don't re-diagnose that class of symptom without checking `df -h`
  first.
  **Cache trap, again**: `compile_shaderc_android` only checks whether
  `libshaderc_combined.a` exists — if it's already there from a PRIOR build (even one before
  this session's newest patch), a rebuild silently reuses the stale archive. This has now bitten
  twice. `rm -f android.wuxing_skills/lib/arm64-v8a/libshaderc_combined.a` before every rebuild
  until/unless the cache check is made to detect script changes (e.g. hash `rlvk_patch_shaderc.py`
  into the cache key) — not done this session, flagged as a real fix worth making if this class
  of iteration continues.
### 7.21 The swapchain recreate loop's real cost, and real Vulkan pre-rotation
After §7.20 finally got every shader compiling, a new symptom appeared: entering any in-game
screen made the entire 2D UI/HUD overlay vanish — buttons, panels, HUD bars all gone — while
3D world content (character, environment, a debug gizmo) kept rendering fine, with **zero**
warnings or errors logged. Investigation:
- A TEMP diagnostic (`UIDBG`, unconditional `TRACELOG` right before the UI-draw block, gated by
  nothing) never fired even once, confirmed via `adb logcat -c` immediately before checking (so
  buffer eviction from the swapchain-recreate spam wasn't the explanation) — the code path
  containing the UI draw calls appears to not run at all some of the time, though the exact
  mechanism (crash? hang? some frames just never reaching that point?) was not conclusively
  isolated before a stronger, connected lead emerged.
- Checked the documented "failed pipeline build must skip the draw" quirk first (§9's known
  driver-quirks list) — ruled out: that specific path DOES log
  (`RLVK: vkCreateGraphicsPipelines => ... SKIPPED`) and would repeat every frame since failed
  keys aren't cached, and nothing like it appeared.
- Pulled a wider adb log and found real `gralloc4`/`GraphicBufferAllocator`/`AHardwareBuffer`
  allocation failures (`Failed to allocate (4 x 4) ... format 59/56 usage b00: 5`), firing in
  lockstep with every `RLVK: swapchain created/recreated` cycle — i.e. the **still-open,
  previously-deprioritized "harmless" per-frame swapchain recreate bug from §7.12** is not
  harmless: it's thrashing Android's own buffer allocator hard enough to produce hard
  allocation failures on-device, now that real shaders/pipelines are actually being exercised
  (previously every custom shader silently used the same default pipeline, never stressing
  whatever this churn touches the same way).
- Confirmed swapchain extent is rock-stable (`2320x1080`, every single recreate, `sort -u` over
  the whole log session) — rules out extent fluctuation as the OUT_OF_DATE cause, consistent
  with §7.12's original hypothesis: `preTransform=IDENTITY` not matching the device's real
  `currentTransform=ROTATE_90` is what some Android/Mali drivers treat as perpetually
  suboptimal, continuously reporting `VK_ERROR_OUT_OF_DATE_KHR`.

**User explicitly chose the full fix over a smaller mitigation** (already committed/pushed to
git as a safety net, so no `checkout` needed before proceeding). Implemented real Vulkan
"pre-rotation" — the standard, Khronos/Google-documented technique for this exact class of
Android driver behavior:
- `rlvkAttachSurface` (`rlvk_platform.inl`) now requests `preTransform == currentTransform`
  (falls back to IDENTITY only if currentTransform itself somehow isn't in
  `supportedTransforms`, which shouldn't happen), and records how many 90° quarter-turns of
  compensation are needed (`RLVK.preRotationQuarterTurns`, new top-level `rlvkData` field —
  **not** inside the nested `RLVK.State` sub-struct, a placement mistake caught immediately by
  `check_rlvk_compile.sh`).
- The compensation itself lives in exactly ONE place: `rlSetMatrixProjection()`
  (`rlvk_compute.inl`). Traced (not assumed) that this is the single choke point every draw
  path funnels through: 2D UI (`SetupViewport`'s `rlOrtho` → this), 3D world (`BeginMode3D`'s
  camera projection → this), AND raylib's own CPU-side `DrawMesh` MVP computation
  (`rmodels.c:1517,1736` call `rlGetMatrixProjection()` — confirmed via `grep` against the
  actual raylib source — which reads straight back from what this function stores). Rotating
  the matrix stored here therefore covers every rendering path with **zero shader changes and
  zero push-constant layout changes** — a dramatically smaller blast radius than the
  alternative (injecting a rotation into the shared push-constant struct + extending
  `rlvkInjectClipZEpilogue` + regenerating the embedded default shader's SPIR-V, which was the
  first design considered and rejected as unnecessarily invasive once this single choke point
  was found).
- **Touch input needs no changes.** Reasoned through this rather than assumed: Android's input
  system delivers touch coordinates in the app's own logical window space
  (`ANativeWindow`-relative, landscape), independent of whatever the Vulkan swapchain's
  `preTransform` is — that value only affects how the GPU-rendered buffer gets composited onto
  the physical panel for *display*. Since the compensation is specifically designed to make the
  final visual output identical to what already worked (same layout, same button positions),
  and touch mapping is already correctly calibrated to that visual layout (§7.12–§7.17), it
  should continue working unmodified. This is the main reason a MUCH smaller mitigation
  (throttling recreate frequency, not fixing the transform) was on the table at all — full
  pre-rotation looked risky specifically because it seemed likely to reopen the touch-alignment
  saga; tracing the actual coupling (or lack thereof) defused that risk.
- **Rotation sign NOT device-verified.** `-90°` per quarter-turn is the best derivation
  available without a live device to iterate against — commented clearly in code as a one-line
  fix (flip to `+90°`) if the on-device result comes out rotated the wrong way. §7.12's own
  history of describing an uncompensated `ROTATE_90` result as "mirrored" (not just rotated)
  hints the true relationship might have another wrinkle beyond a clean single rotation; flagged
  rather than over-fit a guess.
- **Verification**: desktop `check_rlvk_compile.sh` (caught the `RLVK.State.` vs `RLVK.`
  placement bug immediately) + full 13/13 visual suite (desktop/MoltenVK reports IDENTITY
  `currentTransform`, so `preRotationQuarterTurns` stays 0 there — the new code is a pure no-op
  on desktop, and 13/13 staying green confirms zero regression for that path) + full `cmake
  --build build` project build, all green. **Not yet verified on device** — this needs a real
  Android rebuild, and the human should check THREE things together: (1) does the
  `swapchain created/recreated` log spam stop, (2) does the UI reappear, (3) is the visual
  orientation still correct (rotation sign) — a wrong sign would show up as the whole screen
  rotated/mirrored, not a UI-specific symptom, so it should be easy to tell apart from the
  original bug.

**DEVICE-VERIFIED AND REVERTED (2026-07-18)**: the human rebuilt and tested. Results:
- The recreate-spam theory was **half right**: logcat now shows exactly one
  `RLVK: swapchain created (2320x1080, 5 images)` and no repeated `recreated` lines before a
  clean `RLVK: surface detached` a minute later — so matching `preTransform` to
  `currentTransform` genuinely does stop the OUT_OF_DATE loop.
- But the compensation itself was **visually wrong**: on-screen text came out rotated/mirrored
  (UI text rendered sideways) — the `-90°`-per-quarter-turn derivation was not correct as
  written (and given §7.12's earlier "mirrored, not just rotated" observation, likely needs more
  than a sign flip — a flat rotation may not be the whole transform Mali/Android expects here).
- Critically, **the UI/HUD-vanishing bug was still present** even with the recreate spam gone —
  the human confirmed the two symptoms are unrelated: orientation was already correct before
  §7.21 (i.e. before pre-rotation was introduced), and it's specifically the UI-vanishing bug
  that pre-dates this investigation and remains unexplained.
- This disproves the gralloc-thrashing-causes-UI-vanishing hypothesis as *the* explanation (or at
  least means fixing the recreate loop alone isn't sufficient) and demonstrates the pre-rotation
  compensation math needs real device-side iteration to get right, not further reasoning from a
  desk.
- **Reverted** (same day): `preTransform` forced back to `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR`
  unconditionally, `RLVK.preRotationQuarterTurns` hardcoded to 0, `rlSetMatrixProjection` back to
  a plain assignment. Desktop re-verified: `check_rlvk_compile.sh` clean, 13/13 visual suite
  green (pure no-op there either way, since desktop's `currentTransform` is already IDENTITY).
  `RLVK.preRotationQuarterTurns` field and the quarter-turn `switch` in `rlvkAttachSurface` are
  left in place (dead-but-documented) rather than deleted, in case a future attempt wants the
  scaffolding back — see git history around 2026-07-18 for the exact matrix/switch that was
  tried and didn't work if resuming this.

**Still open**:
- The original UI/HUD-vanishing bug is **unsolved** and back to square one investigation-wise —
  it is NOT a rotation/pre-rotation issue. The `gralloc4` allocation-failure lead from this
  section may still be relevant (the recreate loop is real and worth fixing for its own sake:
  wasted per-frame swapchain rebuild cost even setting aside the UI bug) but is no longer a
  credible root-cause explanation for the UI symptom on its own.
- The `UIDBG` TEMP diagnostic in `main.c` is still in place and STILL never fired on device even
  after the recreate loop stopped — this rules out "recreate spam evicting the log line from the
  logcat ring buffer" as the reason it doesn't show, strengthening the case that the UI draw
  block genuinely isn't being reached (crash/early-return/branch-not-taken), not just that its
  log got lost. Next investigation should instrument further upstream of that point to find where
  execution diverges, rather than assume it's a rendering-layer (rlvk) issue at all — this may
  belong to `main.c`/`core/` state, outside rlvk's ownership.
- Real pre-rotation (fixing the OUT_OF_DATE loop properly) remains a valid future improvement but
  needs on-device iteration to get the transform math right — not something to re-attempt blind.
- ROTATE_180 was never observed/tested on this or any device — moot for now since pre-rotation is
  reverted.
- `adb`/on-device testing **is available this session** (device connected, used extensively in
  §7.12–§7.21) — the old "physical-device testing is human-only" note is stale. Still true: the
  human must actually run `make -f Makefile.Android USE_VULKAN=1` and reinstall.

> **Superseded by §7.22.** The "back to square one" framing above was written before direct
> on-device inspection found the actual root cause. Keep §7.21 for the pre-rotation dead-end
> history, but §7.22 is the resolution.

### 7.22 The real root cause: recreate-on-SUBOPTIMAL every-frame loop (2026-07-18)
After reverting §7.21 (back to IDENTITY preTransform), orientation was correct again but the UI
still vanished on in-game screens. This time the investigation was done **directly on-device via
adb** instead of by reasoning, which cracked it:
- `adb exec-out screencap` on the sandbox screen: 3D world (character, planet, health bar,
  selection ring) renders, but no 2D UI **and** no magenta debug box that `main.c`'s UIDBG probe
  draws unconditionally right before the UI block.
- `adb logcat`: the UIDBG `TRACELOG` (a pure-C call, independent of rendering) fired **zero**
  times — proving the frame never reaches that line at runtime — while `strings` on the installed
  `libmain.so` confirmed the probe code (the `s_uiDbgFrame` symbol + the `UIDBG` literal) **was**
  compiled in. So: code present, never executed. The menu (its own simple `BeginDrawing`→2D→
  `EndDrawing`, no render textures) worked fine; only the render-texture/PostFX in-game path
  stalled.
- The lockstep signal: every ~30 ms, `GraphicBufferAllocator`/`gralloc4` allocation failures →
  `RLVK: swapchain created` → `swapchain recreated`, forever. **The swapchain was being recreated
  every single frame.**
- Root cause in `rlvkPresent` (`rlvk_platform.inl`): it recreated the swapchain on
  `VK_ERROR_OUT_OF_DATE_KHR` **OR** `VK_SUBOPTIMAL_KHR`. On this device (portrait-native Mali
  panel, app locked to landscape) `vkQueuePresentKHR` returns `SUBOPTIMAL` on **every** present
  because we request `preTransform=IDENTITY` (which is what composites with correct on-screen
  orientation here) while the surface's `currentTransform` is `ROTATE_90`. That mismatch is
  permanent — recreating yields the identical IDENTITY swapchain and the identical SUBOPTIMAL next
  frame — so recreate-on-SUBOPTIMAL is an unbreakable every-frame rebuild loop. It thrashed
  gralloc and stalled the in-game frames (which, unlike the menu, do mid-frame flushes for the
  PostFX/depth-snapshot render-texture round-trips) badly enough that execution never reached the
  post-PostFX 2D UI draws.

**Fix**: recreate the swapchain ONLY on `OUT_OF_DATE`, never on `SUBOPTIMAL`. `SUBOPTIMAL` means
"presented fine, just not ideal" — the acquire path already treated it as safe-to-render; the
present path now matches. A real invalidation (resize/resume/rotation) still surfaces as
`OUT_OF_DATE` on the next acquire, so nothing that genuinely needs a rebuild is missed. One-line
change (drop the `|| SUBOPTIMAL` from the recreate condition), plus a repro scenario
`ui_after_rt` in the visual suite (renders 3D into an RT, blits it full-screen, then draws 2D on
top — the exact PostFX→UI sequence) which passes, and desktop 14/14 suite green. This keeps
IDENTITY, so orientation stays correct with **no rotation compensation** — §7.21's pre-rotation
was solving the wrong problem (it did stop the loop, but by matching currentTransform, which then
demanded content rotation that was never the actual need).

**Verification status**: desktop-green; **the on-device confirmation is the magenta box +
UIDBG log both appearing on the sandbox screen after this fix** — pending the human's rebuild.
The `preRotationQuarterTurns` field + the quarter-turn `switch` in `rlvkAttachSurface` are now
provably dead (IDENTITY path only) and can be deleted whenever convenient; left in as documented
history for now.

**§7.22 CONFIRMED on device (2026-07-19)**: SUBOPTIMAL fix stopped the recreate/gralloc spam,
orientation stayed correct. All the TEMP probes named here (UIDBG etc.) were removed 2026-07-19.
The `preRotationQuarterTurns` dead scaffolding can still be pruned.

### 7.23 THE Android dim/garbled 2D bug: rlvk pool-ring descriptor path (2026-07-19) — FIXED
After §7.22 the UI still rendered **dim + garbled** on in-game screens (opaque magenta drew at
~18% brightness, text as dark blocks). Root-caused entirely on device (adb screencap + pixel
readback + `strings` on the installed lib), then reproduced on desktop and fixed there:

- **Symptom decode**: on-device screenshot pixel of an opaque-magenta probe box read `(45,0,58)` —
  i.e. `texel × vertexColor` where `texel` ≈ the dark bluish *scene* (G killed because magenta's
  G=0). So 2D draws were sampling a **stale scene render-target at texture unit 0** instead of the
  1×1 white / font atlas. `PUSHTEX0`/`SHDDBG` diagnostics proved the correct texture SLOT and the
  correct default shader + colDiffuse were being used — so the descriptor *binding* was the fault.
- **Root cause** (`rlvk_frame.inl`): rlvk uses a **pool-ring snapshot-descriptor fallback** when
  the device lacks `VK_KHR_push_descriptor` — which Mali-G68 does; MoltenVK/desktop HAS the
  extension and always took the push path, so the fallback was effectively untested and the whole
  visual suite was green on it. In `rlvkPushTexture`, after the dedup passed (binding changed) it
  wrote `RLVK.pushedView[binding] = view` **before** calling the compat shim
  `rlvkPushDescriptorSetCompat`, which detects changes by comparing the incoming view against
  `pushedView`. Since `pushedView` was already overwritten, the shim saw "no change", never set
  `set0Dirty`, and `rlvkFlushSet0` skipped rebinding the set. Net effect: the **second batch draw
  with a different texture kept the previous texture bound** — e.g. the UI text/rects after the
  PostFX composite (which had just bound the scene RT to unit 0) sampled the scene → dim + garbled.
- **Fix**: one line — set `RLVK.set0Dirty = true` in `rlvkPushTexture` right after updating
  `pushedView` (reaching there means the binding genuinely changed). Harmless on the push path
  (`rlvkFlushSet0` early-outs on `Caps.pushDescriptor`).
- **Desktop repro/guard**: forcing the pool-ring path (now `RLVK_FORCE_POOL_RING=1
  ./scripts/run_rlvk_visual_test.sh`, an env toggle added 2026-07-19 instead of editing
  `RLVK.Caps.pushDescriptor` by hand) makes the visual suite exercise the fallback (it FAILED "sprite
  core not white" before the fix). Keep this as a periodic manual test — no dev machine hits the
  fallback naturally. **CAVEAT (2026-07-19): this guard is currently RED on the Intel-Iris/MoltenVK
  dev machine** — the forced fallback skips textured draws entirely there (blue screen), a
  MoltenVK/Metal artifact of the forced path, not a real Mali bug (§8.4b-3 has the full evidence). So
  a green run here can't be assumed; validate pool-ring changes on real Mali until the guard host is
  fixed.
- **Also fixed MetaballFX's total UI loss** (§ earlier this session): that was the SAME bug
  amplified — MetaballFX's extra half-res RT round-trips guaranteed a different texture was bound
  right before the UI, so the stale binding became a *total* loss rather than a dim tint.
- **USER-CONFIRMED in-game**: buttons, panels, readable text all back; MetaballFX re-enabled fine.

### 7.24 Session build-system + gameplay fixes (2026-07-18/19)
- `Makefile.Android`: (a) `-c $^` → `-c $<` (once the `-include`d `.d` files exist, `$^` fed every
  header to clang → "cannot specify -o when generating multiple output files"); (b) raylib.a is now
  rebuilt when any `third_party/vulkan/rlvk/**` or `rlvk_patch_raylib.py` is newer than it (rlvk is
  compiled INTO libraylib.a via the rlgl→rlvk patch, and the old existence-only cache silently
  no-op'd every rlvk edit — this wasted several device round-trips before it was found); (c) added
  missing `core/mesh_adjacency.c` to `PROJECT_SOURCE_FILES` (a new mesh-particle feature linked on
  desktop CMake but not Android).
- Sandbox touch: the "no-cast on the left 45% of screen" was cut to just the joystick zone
  (`sandbox/sandbox_core.c`), so skills cast to the left of the character again.

### 8.4b issues from the 2026-07-19 session
1. **Top sandbox/vfx_test buttons hard to tap — RESOLVED (2026-07-19, device-confirmed).** Root
   cause was NOT letterbox/renderOffset (`GetMousePosition == GetTouchPosition` on device, mapping
   correct). It was the **Android top-84px MANDATORY system-gesture inset** (`dumpsys window` →
   `mandatorySystemGestures frame=[0,0][2400,84] sideHint=TOP`): the buttons sat at y=15, inside that
   band, so the OS intermittently stole the touch (notification-shade pull) → button highlights on a
   leaked down-frame but the tap never reaches the app. Nailed it by comparing an `adb shell input
   tap` on the button (bypasses the OS gesture layer → worked, opened the panel) against a real
   finger tap (logged NOTHING reaching the app). MANDATORY zone can't be excluded, so **fix = shift
   the whole sandbox/vfx_test panel down +80px** (interactive UI now starts at y≥95). Full detail in
   memory [[project_mali_device_landmines]]. Also fixed the main-menu + these buttons' click logic to
   arm-on-DOWN/fire-on-RELEASE (Android's down-frame `GetMousePosition` is stale).
2. **Black-hole VFX 3 swirl shells invisible — RESOLVED (2026-07-19, device-confirmed).** Root cause
   was the shared noise hash, not the black hole's draw path (VS/FS/blend/cull all fine). Cylinder
   aura had the same root (visible membrane, dead swirl). `core/shaders/common/noise.glsl`'s
   `hash*()` used `fract(sin(dot(p,K))*43758.5)`, which loses precision on Mali for large sin
   arguments → hash goes constant → fbm collapses → density/alpha 0 (invisible) or no animated
   detail (static aura). Broke only the effects that push the noise domain far (black-hole fbm3
   octaves; aura_shell's `u_time`-in-domain scroll); small-domain fbm2 VFX were unaffected, masking
   it. **Fix = non-sin Dave Hoskins hash** (see [[project_mali_device_landmines]]). Note for future:
   invisible ≠ shader-compile-fail (that falls back to the default shader and draws WHITE).
3. **~30 FPS on Mali** with the full HDR + ScreenDistort + PostFX(+bloom) + GPU-particle pipeline.
   The pool-ring descriptor path (per-draw `vkAllocateDescriptorSets` + full set rewrite in
   `rlvkFlushSet0`) is inherently heavier than push descriptors and every draw now dirties it more
   (the `set0Dirty` fix). Perf not profiled.
   **Descriptor snapshot cache LANDED (2026-07-19) — the "future win" above, not yet Mali-verified.**
   `rlvkFlushSet0` now keys a per-frame cache (`RLVK.set0Cache[frameIndex]`, `RLVK_SET0_CACHE_SIZE`
   128) on the full snapshot state — `pushedView[]`, `pushedSampler[]`, `shadowUbo[]` (buffer/off/
   range), and the `computeSSBO[0..3]` slots (all deterministic inputs to the written set). A hit
   skips both `vkAllocateDescriptorSets` and the full `vkUpdateDescriptorSets` rewrite, keeping only
   the bind — and even the bind is skipped when the same set is still bound (`RLVK.boundSet0`). On a
   Mali frame this collapses the repeated re-binds of the font atlas / white texture / scene-RT
   (rebound after every PostFX pass) from an alloc-per-flush to one alloc-per-distinct-combo. The
   cache + `boundSet0` are cleared at BOTH pool-reset sites (`rlvk_platform.inl` beginFrame,
   `rlvk_renderpass.inl` flushFrame) since the reset frees every set. Above the 128 cap it falls
   through to the old allocate-every-flush path (still correct; distinct combos/frame stay well under
   it). New env toggles: `RLVK_FORCE_POOL_RING` (force the fallback on a push-descriptor desktop for
   the §7.23 manual test), `RLVK_NO_SET0CACHE` (A/B the cache off).
   **Verified**: desktop `check_rlvk_compile.sh` + normal suite 14/14 green — but the cache is a pure
   no-op on the healthy push-descriptor desktop path (it only runs when `!Caps.pushDescriptor`), so
   14/14 confirms *zero regression*, not the cache itself. Positive validation needs an on-device Mali
   run (or a desktop that lacks push descriptors). Attempting to validate it via `RLVK_FORCE_POOL_RING`
   on this MoltenVK/Intel-Iris machine hit the pre-existing guard breakage below, NOT a cache bug:
   committed code (cache reverted) produces a *byte-identical* failure signature, so the cache is
   behaviorally transparent — a buggy cache returning a wrong set would have diverged.
   - **NEW FINDING — the §7.23 desktop guard is currently RED on this MoltenVK/Intel-Iris machine.**
     `RLVK_FORCE_POOL_RING=1 ./scripts/run_rlvk_visual_test.sh` fails `batch_alpha` + `additive3d` and
     segfaults entering `depth`. Pixel-probed: `batch_alpha` center reads `(0,121,241)` = the pure BLUE
     clear color, i.e. the textured 2D draw is **skipped entirely** (§7.2 "failed pipeline build ⇒ skip
     draw" signature — blue screen, not a stale texture), and no pipeline-bind logs fire even under
     `RLVK_DEBUG_PIPE`. This is almost certainly a MoltenVK/Metal artifact of the forced fallback on
     *this* device (the non-push set-0 layout = 16 combined samplers + 4 SSBOs may trip a per-stage
     Metal argument limit that real Mali clears), NOT a real Mali regression — §7.23 was user-confirmed
     in-game on Mali and the healthy desktop path is 14/14. But it means **the forced-pool-ring guard
     cannot validate any pool-ring change on this machine right now** — next device session should
     (a) confirm the descriptor cache renders correctly on real Mali, and (b) decide whether the desktop
     guard needs a different host (an Android emulator / a non-Intel MoltenVK) or a documented "skip on
     Intel-Iris" caveat. Did NOT chase the MoltenVK skip further: out of the perf task's scope and not a
     real-hardware bug.

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
  `VALIDATE=1 ./scripts/run_rlvk_visual_test.sh` (13/13, `grep -c 01211` == 0).

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

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-08-16 | Codex | §7.33 runtime teardown | `rlvk_core.inl`, `rlvk_shader.inl`, `tests/rlvk_runtime_test.c` | Ground-truth |
