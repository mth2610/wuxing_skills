# rlvk — Progress / Backlog

> Status + remaining work for the rlvk Vulkan 1.1 backend. Full narrative and per-item evidence in [`HANDOFF.md`](HANDOFF.md) §8; the debugging log is §7 (indexed in `LANDMINES.md`).

## Perf measurement — HOW, and two traps that produce confident nonsense (2026-07-22)

`UNCAPPED=1 ./scripts/run_rlvk_visual_test.sh perf_<name>` (IMMEDIATE present; under FIFO every
number is just a vsync bucket, and the game additionally caps with `SetTargetFPS(60)` — build it
with `-DWUXING_PERF_CAPTURE=ON` and read the **ms** HUD, never FPS).

**Trap 1 — never time variant A as one block and variant B as the next.** Presentation throttling
changes phase during a run: the same `perf_dynmesh` work measured **10 ms in whichever block ran
first and 1.8 ms in whichever ran second**. Swapping the order swapped the numbers. Any A/B taken
as consecutive blocks on this platform is worthless.

**Trap 2 — do not alternate variants with `f & 1`.** `RLVK_FRAME_INDEX_COUNT` is **2**, so
alternating pins each variant to its own frame-ring slot. That bias reported "1 upload costs
4.2 ms, 2 uploads cost 1.8 ms" — an impossibility that looked like a real measurement.

**The method that works**: interleave the variants within one run and pick each frame's variant
from an LCG, so neither presentation phase nor the frame ring can correlate with the variant. Two
independent runs then agree to ~0.3 ms. `sc_perf_dynmesh` is the worked example — copy its shape.

### Numbers that were taken correctly
- **A dynamic mesh re-upload costs ~0.5–0.65 ms per `UpdateMeshBuffer` CALL** (1681-vertex mesh,
  40 KB per buffer; two calls = 1.0–1.3 ms/mesh/frame). 40 KB cannot cost that — the cost is
  `rlvkUploadBuffer` tearing down and rebuilding the render pass **per call**
  (`rlDisableFramebuffer` + `vkCmdEndRenderPass` + copy + barrier + resume). It is per-CALL, not
  per-vertex. Anything doing raylib's `UpdateModelAnimation` (CPU skinning + full vertex re-upload,
  2–3 buffers) pays this per character per frame. **Open optimization**: coalesce consecutive
  uploads into a single pass split (lazy resume) — and, game-side, GPU skinning removes it entirely.
- **The §7.29 twin-fix delta**: `perf_rt2048` 13.4 → 8.9 ms, corroborated in-game (33 → 21 ms).

- **Extra full-resolution offscreen passes are free.** `perf_fullres_ab` (1 vs 3 × 1280×720
  RGBA16F, LCG-interleaved): −0.80 and +0.28 ms per extra pass across two runs. Collapsing the
  postFX/distort chain is not worth doing.
- **An upload inside an FBO scope costs no more than one on the swapchain.** `perf_upload_fbo`:
  −0.39 / +1.08 ms across two runs, i.e. noise. The theory that every `rlvkUploadBuffer` suspend
  re-runs the depth-twin bounce for the active render target is **tested and rejected** — do not
  re-chase it.
- **The step that IS large: rendering into a 1280×720 offscreen target at all.** Drawing the same
  mesh to the swapchain is ~0.9 ms/frame (`perf_dynmesh` draw-only); the moment the scene goes
  through a full-res target the frame is ~9 ms, reproduced across `perf_fullres*`,
  `perf_hdr_main` and `perf_upload_fbo` in separate sessions. Since *extra* passes are free, this
  is fill/resolution-bound, not pass-count bound — the lever is internal resolution (and the 2048²
  shadow capture, which is 4.5× the scene's pixel count).

### The shadow system: no single hotspot (2026-07-22, user-confirmed `J` off ⇒ >60 FPS)
In-game the whole shadow subsystem is worth ~5 ms (20 ms → under 16.6 ms when toggled off). Probing
its parts found **no dominant one** — it is the sum:

| part | A/B probe | cost |
|---|---|---|
| capture map 2048² vs 1024² | `perf_shadow_ab` | **0.6 – 1.1 ms** |
| ground receiver 16 vs 4 PCF taps @1280×720 | `perf_pcf_ab` | **0.16 – 0.89 ms** (full-screen; the ground covers less) |
| the rest (~3 ms) | not isolable in the harness | the **second full scene traversal** the capture pass costs — draw calls, binds, vertex work, all doubled |

So the lever is not resolution and not the filter: it is **how often the scene is re-drawn into the
map**. Capturing on alternate frames halves the whole capture cost, CPU draw calls included.
NOTE: `perf_shadow_ab`'s first version drew one small cube and reported resolution as free — a
capture probe must make the casters FILL the map, as the fitted light frustum does in-game.

### Session outcome (2026-07-22, in-game, Intel Iris 6000)
33 ms → 21 ms (§7.29 twin elision) → 17 ms (half-rate shadow capture + 8 PCF taps) → **16 ms /
62 FPS** (twin keep-alive window + idle-gated `ScreenDistort_SnapshotDepth`).

**Half-rate shadow capture was REVERTED (same day).** It bought ~1.5 ms but produced visible
shimmer on moving casters (30 Hz shadow vs 60 Hz motion, worse wherever the frame rate is lower)
*and* the alternating frame cost below. Temporal tricks on the shadow update rate are not free
here; take the milliseconds from resolution (spatial) instead. Confirmed on device (Mali-G68).

**Watch frame PACING, not just the average.** Half-rate shadow capture makes frame cost alternate
heavy/light — a 16 ms average with a 19 ms worst is capture frames at ~19 and skipped frames at
~14. Under vsync every heavy frame misses the 16.6 ms deadline, which can feel like judder even
though the average clears 60. If that shows up, prefer an evenly-distributed cost (e.g. a 1024²
capture EVERY frame) over an alternating one at the same average.

### Numbers that are NOT trustworthy — retake them with the LCG method before citing
`perf_base`, `perf_switch*`, `perf_rt256`, `perf_fullres*`, `perf_hdr_*`, `perf_ldr_bloom` were all
taken one-config-per-process (trap 1). They suggested that scope switches, extra full-res passes,
the bloom pyramid and the HDR format were all free — **the dynmesh result contradicts at least the
"pass splits are free" part of that**, since the upload cost IS a pass split. Treat those as
unmeasured.

## Format capability query added — R32F blend/filter are no longer assumed (2026-08-11)

rlvk called `vkGetPhysicalDeviceFormatProperties` **nowhere**: `rlvkGetVkTextureFormat`
mapped `RL_PIXELFORMAT_UNCOMPRESSED_R32` to `VK_FORMAT_R32_SFLOAT` and every caller took
the remaining features on faith. Per the spec's Mandatory Format Support tables that faith
is misplaced for exactly the two features screen-space passes lean on: `R32_SFLOAT`
guarantees `SAMPLED_IMAGE` + `COLOR_ATTACHMENT` but **not** `COLOR_ATTACHMENT_BLEND` and
**not** `SAMPLED_IMAGE_FILTER_LINEAR`; `R16_SFLOAT` guarantees both.

Added: `rlvkFormatSupportsColorAttachment/Blend/LinearFilter(int rlFormat)` (public,
`rlvk_format.inl`), `Caps.floatBlendR32` / `Caps.floatFilterR32` cached at init with a
one-time warning, 6 runtime-test checks, and visual scenario `float_blend_rt` asserting the
cap matches observed behaviour (three additive writes must read back 3.0).

Measured on MoltenVK/Metal here: `R32F blend=1 linearFilter=1`, additive x3 → 3.000. So the
desktop path is genuinely fine — this is instrumentation for the driver that is not, and
the caller-side decision (which format a pass should use) still belongs to the caller.
Known consumer that relies on both today: `core/fluid/fluid_surface.c` (four R32F targets,
additive thickness pass, BILINEAR filter) — Core's call.

## Compute dispatch costs ~0.6-0.9 ms per CALL — and it is none of the obvious three (2026-08-11)

core/fluid's PBD solver measured **4.4 ms in-game** for 2,048 particles across 9
dispatches — 72 workgroups of real work. `perf_dispatch_count` (visual suite,
LCG-interleaved) reproduces the shape with a kernel that does nothing but add a
float: **1 dispatch 1.316 ms, 9 dispatches 6.240 ms, i.e. 0.615 ms per extra
dispatch.**

Three hypotheses were tested and **all three are wrong**, so do not re-chase them:

| suspect | test | result |
|---|---|---|
| one-shot submit + `vkQueueWaitIdle` (out-of-frame path) | 9 dispatches in-frame vs outside | 0.02 ms/call difference |
| render-pass split per dispatch (in-frame path) | same measurement, other direction | same |
| the full `CmdPipelineBarrier2` after every dispatch | `RLVK_EXP_NO_COMPUTE_BARRIER=1` (gate since removed) | within run-to-run noise (±0.5 ms) |

**Next suspect, untested: the per-dispatch descriptor set.** Every dispatch calls
`vkAllocateDescriptorSets` and rewrites the whole snapshot (SSBOs, storage
images, samplers, uniform block). It is the only remaining heavyweight step both
paths share, and MoltenVK builds a Metal argument buffer per set. The experiment
that would settle it: cache the set and re-use it while the bindings and the
uniform generation are unchanged, then re-run `perf_dispatch_count`.

**CORRECTION (same day, from the game): batching is worth ~3.9 ms and the
synthetic scenario was WRONG about it.** In-game with the PBD fixture:
5.6 ms of fluid before, **1.7 ms after** (60 -> 45 FPS became 60 -> 54-55).
`perf_dispatch_count` reported the change as noise, so its "outside the frame"
variant cannot be reaching the path the game reaches — `rlEnableShader` and
friends call `rlvkBeginFrame` lazily ("ensure a frame is active"), so a scenario
that sets up its dispatch state inside a render loop is probably in-frame no
matter where the call sits. **Do not trust that scenario's out-of-frame column
until it proves which path it took** (methodology rule 4: distrust your own
probes — this is a worked example of it). The three ruled-out suspects above
still stand: they were measured on the in-frame path, which the scenario does
reach.

**Landed:** out-of-frame dispatches now batch into ONE command buffer
(`RLVK.computeBatch`) instead of each creating a command pool, submitting,
waiting on the queue and destroying the pool. Correct by construction — the
per-dispatch memory barriers still order the work — and flushed before anything
that must observe it: any other one-shot submission (`rlvkOneShotBegin`), frame
begin (**before** the compute descriptor-pool reset, which would otherwise free
sets the pending buffer references), and `rlglClose`. Worth ~3.9 ms in the
game, which is where it was finally measured correctly.

## State
Retarget 1.3→1.1-core complete. Headless suite runtime-verified on MoltenVK (20/20, zero validation errors); visual suite 14/14. **In-game confirmed on desktop** (character self-occlusion, black-hole occlusion, soft-particle fade). **Runs on real Android/Mali hardware** (2026-07-17); the Android bring-up bugs (HANDOFF §7.11–7.23) are fixed.

## Immediate-mode normals verified faithful (`imm_normal`, 2026-08-06)
Closed the volume-tube `|N·V|` investigation from the backend side: the handoff's
conclusion ("rlNormal3f does not deliver per-vertex normals") is a **false alarm**. The
`imm_normal` scenario sends a known normal down the game's exact immediate-mode draw path
and reads it back numerically: raw attribute = `view*N` (d 0.002), `matModel*` =
`view*view*N` (d 0.005). The backend is provably faithful — `transformRequired` CPU-rotates
attributes into view space and the flush uploads `matModel = State.transform`, so the old
`matModel * vertexNormal` double-rotated. Core-side fix (attributes passed through, frag
stage takes `V = normalize(-fragPosition)`) landed and is guarded by
`imm_normal` + `core/tests/volume_space_contract_test.c`. → HANDOFF §7.8.

## Done
- **Depth / occlusion in render textures** (§7.1) + **soft-particle soft-cut** (§7.10) — user-confirmed in-game.
- **Graphics-stage SSBO** (§8.2) — `ssbo_vs` 11/11, zero validation errors; read-only graphics SSBOs on weak 1.1 devices via `Caps.graphicsSsboStores`.
- **Port `compute/gpu_particle_system.c`** (§8.3) — nothing to do; already pure `rl*` API, lights up under Vulkan with §8.2 done.
- **Android platform glue** landed; swapchain/letterbox/touch/shaderc/descriptor-path bugs fixed.
- **Descriptor snapshot cache** (§8.4b) landed — per-frame set0 cache to cut per-draw `vkAllocateDescriptorSets` on the pool-ring path.

## Open / next
- **Confirm GPU particles in the actual game** (human-run build) — the one unverified end-to-end path after §8.2.
- **Descriptor cache correctness on real Mali** — it's a no-op on the healthy push-descriptor desktop, so 14/14 proves zero-regression, not the cache working. Needs an on-device run.
- **Desktop pool-ring guard is RED on this MoltenVK/Intel-Iris host** (§8.4b) — `RLVK_FORCE_POOL_RING` can't validate pool-ring changes here (a Metal argument-limit artifact, not a Mali regression). Decide: different guard host (Android emulator / non-Intel MoltenVK) or a documented "skip on Intel-Iris" caveat.
- **Perf**: ~30 FPS on Mali with the full HDR + ScreenDistort + PostFX(+bloom) + GPU-particle pipeline; not profiled/optimized. Descriptor cache is the first lever (unverified on Mali).

## Deliberate known gaps (§8.5)
- `rlLoadShaderProgramEx` unimplemented (unused by raylib's normal flow).
- `rlBindImageTexture` records but plain textures lack STORAGE usage/GENERAL layout — dedicated path when a real consumer appears (blanket STORAGE kills mobile framebuffer compression).
- Compute samplers limited to 2 (bindings 12–13).
- Cache limits warn when exhausted (`RLVK_MAX_RENDER_PASSES` 32, framebuffers 64, desc sets 1024/256) — tune against real content.
- Shadow-copy twin bandwidth (§7.10): the depth→R32F bounce runs at every `rlDisableFramebuffer` of a depth FBO even when never sampled (quirk drivers only) — future opt = lazy first-sample creation.
- Validation noise: `01211` ×30 fixed → suite 0; only the intentional §7.5 stride-0 `04457` ×3 remains.

## Long-term (§8.6, standalone engine)
Tiler-aware VFX (load/store ops as real levers), then extract `core/` VFX + `compute/` + `environment/` into a library whose only downward interface is rlgl + platform hooks. GL-vs-Vulkan stays a build-time choice per binary; runtime selection needs a shared-library split (deferred).
