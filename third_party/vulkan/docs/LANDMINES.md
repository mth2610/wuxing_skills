# rlvk — Landmines

> **Read this before starting any rlvk bug hunt.** rlvk is the hardest thing in the repo to debug; almost every new bug rhymes with one below. Each entry is a scannable Symptom → Rule; the **full debugging chain (symptom → what it looked like → root cause → fix → guard) is in [`HANDOFF.md`](HANDOFF.md) §7**, referenced per row.
>
> Verify via the ladder, never the game first: `check_rlvk_compile.sh` → `run_rlvk_runtime_test.sh` → `run_rlvk_visual_test.sh [scenario]` → (human) full build. Every bug below reproduces in a ≤40-line scenario in seconds.

## Debugging methodology (the meta-rules — HANDOFF §6)
1. **Reproduce in a micro-scenario before reading code.** Reading tells you what the code *intends*; only execution tells you what the driver *does*. Code-reading found nothing for §7.1; four env-gated experiments found it in minutes.
2. **Zero validation errors ≠ correct.** Validation catches *illegal* Vulkan, not *wrong* Vulkan (§7.1/7.2/7.4 were all silent). Inverse too: a wall of VUIDs usually has ONE root cause — fix the FIRST failure in the chain (§7.3).
3. **Bisect with single-variable, env-gated experiments** compiled into the backend (`getenv("RLVK_EXP_X") ? a : b`), rerun the one failing scenario. **Remove the gates once solved** — permanent switches rot.
4. **Distrust your own probes.** View the actual screenshot before believing a numeric probe; a probe in the wrong layout returns convincing garbage. When a probe contradicts other evidence, validate the probe first.
5. **Fail visibly-safe, never silently-garbage.** A failed `rlvkBindPipeline` must draw *nothing*, not leave the previous pipeline bound (§7.2 — squares pointed at blending, miles from the cause).
6. **Driver quirk → a `Caps.*` flag + a repro scenario, not an inline hack.** Never penalize healthy drivers with an unconditional change; never leave a quirk as chat-log folklore.
7. **Suspect state *lifecycles* before state *values*.** Every GPU-fault (§7.3/7.6) was ring/fence/semaphore/frameCounter desync — the values always looked "plausible". Audit who advances the ring and every early-return between acquire and present.

## Trap index (check before a new hunt — full chain in HANDOFF §7.x)

### MoltenVK / driver quirks (desktop macOS)
- **Depth test silently off inside a render texture** (see-through characters, black-hole ring not occluded). SAMPLED-usage depth attachments don't test on MoltenVK → `Caps.noSampledDepth` + shadow-copy twin. → §7.1, §7.10
- **~~Second push-descriptor to the same binding is dropped~~ — WRONG DIAGNOSIS; real cause was an `rlPushMatrix` transform leak.** A custom-shader immediate-mode draw (P6 ground shadow receiver) rendered blank whenever a prior 3D draw used `rlPushMatrix/rlPopMatrix` (e.g. `DrawCube`). It *looked* like the texture push was dropped (shader sampled the default white), but the descriptor was correct on both the push AND the bound-set path — the corrupted state was the shader's `u_lightVP` uniform. `rlPopMatrix` gated its `transformRequired` reset on the shared `stackCounter==0`, which never fires while `BeginMode3D`'s PROJECTION push is outstanding → `transformRequired`/`currentMatrix=&transform` leak into the next custom-UBO draw. **FIXED** via a MODELVIEW-only push-depth counter (`State.mvStackDepth`). → §7.26. Repro/guard: `run_rlvk_visual_test.sh shadow_pipeline`.
- **Compute UBO reads all zeros** when the shader also declares storage images → MoltenVK quirk; split/rearrange bindings. → §7.4
- **Vertex attribute fetch reads zeros / GPU timeout on a tiny dummy buffer.** → §7.5

### Format capability (what the driver is allowed NOT to support)
- **An R32F render target that is blended into or LINEAR-sampled is optional behaviour, and its absence is silent.** The spec's Mandatory Format Support tables require `COLOR_ATTACHMENT_BLEND` and `SAMPLED_IMAGE_FILTER_LINEAR` for `R16_SFLOAT` but **not** for `R32_SFLOAT` (only `SAMPLED_IMAGE` + `COLOR_ATTACHMENT` are guaranteed there). rlvk queried no format features at all until 2026-08-11, so `rlvkGetVkTextureFormat` handed out `VK_FORMAT_R32_SFLOAT` and every caller assumed the rest. Desktop drivers and MoltenVK do provide it — which is exactly why nothing catches it before a mobile driver. **Rule:** ask `rlvkFormatSupportsBlend()` / `rlvkFormatSupportsLinearFilter()` before relying on either, and fall back to R16F or unorm (both mandatory) when false. `Caps.floatBlendR32` / `Caps.floatFilterR32` are cached at init and the init log warns once. Scenario `float_blend_rt` asserts the caps agree with what the device actually does (3 additive writes must read back 3.0).

### Anti-aliasing
- **`FLAG_MSAA_4X_HINT` / `rlvkSetMsaaSamples` anti-alias NOTHING in an engine that renders its scene into an FBO.** They configure the swapchain; the game rasterizes into `ScreenDistort`'s offscreen HDR target and only composites a quad to the swapchain, so the multisampled surface was the one nothing was drawn into. Offscreen MSAA is opt-in per framebuffer: `rlvkSetFramebufferSamples(fbId, 4)`. → §7.34, scenario `msaa_rt`. Promoted to `ENGINE_LANDMINES.md`.
- **A multisample DEPTH attachment cannot be brought back to 1 sample in Vulkan 1.1 core.** `vkCmdResolveImage` is colour-only and `vkCmdCopyImage` rejects `samples > 1`, so an FBO whose depth is sampled afterwards (soft particles, the `Caps.noSampledDepth` twin) needs `VK_KHR_depth_stencil_resolve` + `vkCreateRenderPass2`. `Caps.depthResolve` gates it; without it the FBO is refused MSAA rather than given a dead depth buffer. Colour MSAA alone is not an option — one subpass, one sample count.
- **Do not expect MSAA to fix a bright emissive VFX edge, and do not read that as MSAA being broken.** The resolve averages in linear HDR and the tone curve then compresses it: measured on SHIELD SHELL, the 25%-coverage pixel and the 100%-coverage pixel land 5 luma apart, so a +64 step stays a +64 step one pixel over. Same frame, opaque map geometry: steps >50 fall 325 → 97. A tonemapped/weighted resolve would fix it; the fixed-function subpass resolve cannot do one. **Also untouched: features thinner than a pixel** (a 1–2 px shader rim line, a specular streak) — MSAA shades once per pixel, so those are a `smoothstep`/`fwidth` problem, not a sample-count problem. → §7.34
- **Never judge anti-aliasing from the single hardest pixel, or from one scan axis.** The map ellipse first measured as "not one byte changed" — from a horizontal scan through its max-gradient pixel, which sits where the boundary runs nearly horizontal, so no sample count could ever show gradation there. A vertical scan four rows away had a clean `7.9 → 29.1 → 29.1 → 50.2` half-coverage pair, and hard steps over the whole region fell 78 → 25. Count steps over a REGION, both axes. Methodology rule 4, worked example. → §7.34
- **Every render-pass end resolves, not every frame.** rlvk reopens an FBO scope with `loadOp LOAD` whenever the target is closed and reopened, and the colour+depth resolves run at each close: measured **+5 ms per extra reopen** at 1280x720 RGBA16F on Intel Iris 6000. It also forbids `TRANSIENT`/lazily-allocated multisample images, which is what would have made this nearly free on a mobile tiler. `perf_msaa_off` / `perf_msaa_4x` (`UNCAPPED=1`).

### Pipeline / draw safety
- **A blend-state toggle around a batched draw does nothing unless the batch flushes inside it.** `rlDisableColorBlend(); DrawTexturePro(...); rlEnableColorBlend();` re-enables blending before the queued draw is ever submitted, so the draw blends after all — identical to `glDisable(GL_BLEND)` under GL, so this is an API footgun on BOTH backends, not an rlvk quirk. Found while building `bright_vfx`: the composite came back multiplied by the scene target's accumulated alpha (additive VFX push it above 1.0) and read back post-ACES values above 1.0, which that curve cannot produce. `core/post_fx.c` had the same shape at its bloom prefilter and its final composite; the latter was over-brightening every VFX region and clipping it to white. **Rule:** `rlDrawRenderBatchActive()` before restoring the state. Scenario `colorblend_flush` pins both halves. Promoted to `ENGINE_LANDMINES.md` #16.
- **Rendering into an FBO, switching to another FBO, then sampling the first gives white/black output.** Closing a Vulkan render pass is not a layout transition; switch the outgoing colour image to shader-read first. → §7.32 (`fbo_switch`)
- **A colour-layer `ClearBackground()` erases scene occlusion despite `rlDisableDepthMask()`.** rlvk's explicit clear must honour the active depth write mask, just as `glClear` does. → §7.31 (`depth_mask_clear`)
- **Particles/VFX render as opaque squares with black borders** — looked like a blend/alpha bug; was a *stale pipeline* (failed build left the old one bound, wrong shader). Skip the draw when bind fails. → §7.2
- **`MODELVIEW rlPushMatrix()/rlPopMatrix()` inside a non-swapchain framebuffer corrupts a later unrelated draw.** → §7.25
- **`rlPushMatrix()` does not hand back an IDENTITY** — a third member of the §7.8/§7.25/§7.26 family, and this one is stock rlgl semantics rather than an rlvk deviation. The push redirects writes to the persistent `State.transform` and saves whatever it already held; nothing clears it, and the "pop restores it, so it recurses to identity" invariant only holds while pushes are strictly LIFO — PROJECTION and MODELVIEW share one stack. Measured 12/08/2026 in the SSF capture: `transform` held a leftover VIEW matrix (Z translation = camera distance) and every `rlPushMatrix; rlTranslatef; DrawSphereEx` body was view-transformed twice. Any immediate-mode 3D draw building its own transform must `rlLoadIdentity()` first. → full entry in root `ENGINE_LANDMINES.md`.
- **A VFX comes out as small rectangles in scrambled positions/colors, intermittently, worse with more instances or while the camera moves** — NOT a geometry or blend bug: the per-frame arena filled up and the UBO push was **skipped**, so the draw ran on the previous push (stale `mvp` + stale uniforms). Any effect that changes a uniform *per instance* (one flush per instance) triggers it first. Call sites that can drain must reserve the UBO block up front. → §7.28, scenario `ubo_arena`

### Ring / lifecycle GPU faults
- **Device lost (GPU timeout) after heavy VFX ran a while** — ring/fence lifecycle desync, not a value bug. Fix the FIRST unchecked step (`vkAcquireNextImageKHR`). → §7.3
- **Present/readback lifecycle faults.** → §7.6
- **`VUID-…-oldLayout-01211` ×30 in the validated suite** — two independent layout-transition causes. → §7.9

### Shutdown cleanup
- **Symptom:** `run_rlvk_runtime_test.sh` passes but validation reports `VUID-vkDestroyDevice-device-05137` for compute objects.
- **Cause:** `rlUnloadShader()` intentionally retains linked compute programs, while `rlglClose()` omitted their `compMod` and `computePipeline` cleanup in `rlvk_core.inl`.
- **Rule:** every new device-owned object must have a shutdown destroy path, including objects deliberately retained by public API semantics. → HANDOFF §7.33

### Perf traps
- **§7.27 (FIXED 2026-07-22, see §7.29) — the `Caps.noSampledDepth` depth twin was bounced at EVERY scope close, even when nothing samples it.** Fix: a sticky `sampleWanted` flag latched by `rlvkResolveTexBinding`; until something actually binds the twin, scope close emits no depth barriers and no copies. Measured on a 2048² RT: **13.4 → 8.9 ms/frame**. Also note the doc claim below that the cost is "unchanged by resolution" was **wrong** — it was never measured; `perf_rt256` vs `perf_rt2048` shows it scales with pixels. Historical analysis follows.
- **Measure frame TIME, never FPS.** With FIFO present (+ `SetTargetFPS`), 17 ms and 33 ms both report "30 FPS": every partial win looks like it did nothing until the last one crosses the refresh interval. `UNCAPPED=1 ./scripts/run_rlvk_visual_test.sh perf_rt2048` (IMMEDIATE present) for the backend; `-DWUXING_PERF_CAPTURE=ON` for the game.
- *(historical)* **§7.27 original analysis — the depth twin bounce.** `rlDisableFramebuffer` does `depth-image → sampleScratch buffer → R32F twin` for any depth attachment that has a twin, unconditionally — `width*height*4` image→buffer **plus** buffer→image. For a 2048² shadow-map FBO whose depth is only ever depth-TESTED (env_shadow samples its own R32F *color* attachment) that is **~32 MB/frame of pure waste**, and it is the prime suspect for "enabling the real shadow costs ~16 ms" on MoltenVK/Intel. Signature that points here: the cost is **per-pass**, unchanged by resolution *or* geometry, and only removing whole passes moves it.
  **Two obvious fixes were tried and BOTH regress — do not repeat them:**
  1. *Lazy bounce* (only bounce twins something sampled): desyncs the depth image's layout bookkeeping → reintroduces `VUID-…-oldLayout-01211` (the §7.9 class). 0 → 8 VUIDs on `soft_depth`.
  2. *Honour `useRenderBuffer=true` by skipping twin creation*: **raylib's own `LoadRenderTexture` passes `true`**, so this strips the twin from every render texture and breaks soft-particle depth sampling. 0 → 8/9 VUIDs.
  A correct fix must keep the depth image's layout transitions consistent whether or not the bounce runs, and must not key off `useRenderBuffer`. Gate any attempt on `VALIDATE=1` for `soft_depth`/`soft_ground`/`depth_rt` (baseline: 0/1/0).

### Layout / soft-depth
- **Soft particles hard-cut against geometry under `Caps.noSampledDepth`** — the shadow-copy twin of §7.1. → §7.10
- **Adding a soft-depth sampler turns particles into squares / leaves hard intersections** — never assume raylib's `texture0` is descriptor binding 0; shaderc may move it when another sampler exists. Resolve it by reflected name. → §7.30 (`sampler_pair`)

### Bisection discipline
- **Invisible GPU particles — a masterclass in confounded bisection** (multiple overlapping causes; how to un-confound). → §7.7
- **False alarms not to re-chase** — things that look like rlvk bugs but aren't. → §7.8
- **"'rlNormal3f doesn't deliver normals' is a FALSE ALARM"** — immediate-mode normals arrive fine, just **view-transformed**: `MyBeginMode3D`'s MODELVIEW `rlPushMatrix` arms `transformRequired`, so `rlVertex3f`/`rlNormal3f` CPU-rotate into view space AND the flush uploads `matModel = State.transform` = the same view matrix → a shader's `matModel * N` double-rotates. "Many colours" probe was a debug-view compositing trap. → §7.8, scenario `imm_normal`

### Android bring-up (Vulkan on NDK/Mali)
- **Swapchain rotated / mispresented on first real-hardware run.** → §7.12, §7.21, and the real cause: **recreate-on-`SUBOPTIMAL` every-frame loop** → §7.22
- **Touch coordinates misaligned independent of rendering** (`SetupFramebuffer`); the root cause was **`CORE.Window.render`/`screen` set to the wrong size for Vulkan's model** — fix is a uniform-scale letterbox, keep `CORE.Window.screen` synced. → §7.13, §7.14, §7.15, §7.16, §7.17
- **shaderc on device** (statically-linked NDK shaderc); GLES-conversion + glslang gaps were the dominant blockers. → §7.18, §7.19, §7.20
- **Android dim/garbled 2D** — rlvk pool-ring descriptor path (FIXED). → §7.23

## Architecture invariants to preserve (HANDOFF §9)
These are decisions, not accidents — don't "simplify" them away: driver quirks live behind `Caps.*` flags with a repro scenario; error paths produce nothing (never leftovers); the ring/lifecycle is the single owner of frame progression. Full list in `HANDOFF.md` §9.

### Measurement traps
- **A perf scenario cannot leave the frame from inside a render loop.** `perf_dispatch_count`'s "outside the frame" variant dispatches before `BeginDrawing`, and reported batching out-of-frame compute as noise — while the same change was worth **3.9 ms in the game**. `rlvkBeginFrame` is called lazily from several places (`rlEnableShader`, `rlClearBackground`, the render-pass helpers) with the comment "ensure a frame is active", so setting up dispatch state inside the loop very likely opens the frame first and the variant measures the in-frame path twice. A scenario that means to compare paths must PROVE which path it took (log it, or assert on a counter) before its numbers mean anything. Methodology rule 4, worked example.

- **`perf_*` scenarios need `UNCAPPED=1`, and they will not tell you if you forget.**
  Without it the swapchain is FIFO and every iteration blocks on the display, so
  the scenario reports the refresh interval. `perf_ssf_filter` run capped
  reported 11.09 ms for the native filter, 11.55 ms for the same filter at a
  quarter of the pixels, and 10.96 ms with a 3x wider kernel — three
  configurations, one number, all of it the monitor. Uncapped, the same
  comparison shows a real 1.2–2.0 ms resolution delta. The script's own header
  documents `UNCAPPED=1`; the failure mode is that plausible-looking numbers come
  out either way.
- **A missing shader file does not make a scenario fail.** raylib substitutes its
  DEFAULT shader and returns a valid non-zero id, so `if (shader.id == 0)` never
  fires. `perf_ssf_filter` loaded `core/fluid/shaders/fluid_depth_narrow_range.fs`
  by RELATIVE path while the harness runs from `/tmp/rlvk_visual_cache` — it had
  been benchmarking the default shader for its entire existence. Fixed with
  `RLVK_REPO_ROOT` (exported by the script) plus a guard against
  `rlGetShaderIdDefault()`. Any scenario touching a repo asset needs both.
- **`RLVK_GPU_TRACE` reports 0.000 ms on MoltenVK, and it is the host, not the
  code.** The queue family advertises `timestampValidBits=64` and
  `timestampPeriod=1.0 ns`, so the obvious diagnosis — "timestamps unsupported" —
  is wrong. The harvest now uses `VK_QUERY_RESULT_WAIT_BIT` plus
  `WITH_AVAILABILITY_BIT` and warns once if a query is unavailable; on this host
  every query comes back **available with a value of zero**. MoltenVK is
  advertising counters it does not fill. Treat GPU-side timing as unavailable on
  macOS and use Instruments / Xcode's Metal frame capture when a real GPU
  breakdown is needed.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-08-16 | Codex | Shutdown cleanup | `rlvk_core.inl`, `rlvk_shader.inl`, `tests/rlvk_runtime_test.c` | Ground-truth |
| 2026-08-18 | Claude (Renderer Agent) | Anti-aliasing | `rlvk_renderpass.inl`, `rlvk_texture.inl`, `rlvk_frame.inl`, `tests/rlvk_visual_test.c msaa_rt`/`perf_msaa_*`, measured captures | Ground-truth |
