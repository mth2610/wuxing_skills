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

### Numbers that are NOT trustworthy — retake them with the LCG method before citing
`perf_base`, `perf_switch*`, `perf_rt256`, `perf_fullres*`, `perf_hdr_*`, `perf_ldr_bloom` were all
taken one-config-per-process (trap 1). They suggested that scope switches, extra full-res passes,
the bloom pyramid and the HDR format were all free — **the dynmesh result contradicts at least the
"pass splits are free" part of that**, since the upload cost IS a pass split. Treat those as
unmeasured.

## State
Retarget 1.3→1.1-core complete. Headless suite runtime-verified on MoltenVK (20/20, zero validation errors); visual suite 14/14. **In-game confirmed on desktop** (character self-occlusion, black-hole occlusion, soft-particle fade). **Runs on real Android/Mali hardware** (2026-07-17); the Android bring-up bugs (HANDOFF §7.11–7.23) are fixed.

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
