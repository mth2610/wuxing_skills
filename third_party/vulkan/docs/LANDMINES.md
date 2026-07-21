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
- **Second push-descriptor to the same binding within one render pass is dropped** (a custom-shader immediate-mode draw that binds a texture via `rlSetTexture` samples the DEFAULT WHITE texture when *any* prior 3D draw already pushed binding 0 in the same pass; first draw works, later ones don't). UBO push is unaffected when it's the pass's first set-0 UBO push. **UNFIXED.** Repro: `run_rlvk_visual_test.sh shadow_pipeline` (FAILs; delete its two pollution DrawCubes → PASSes). This is what silently broke Real-Shading-P6 ground shadows. Fix dir: pool-ring for mid-pass binding changes (currently `RLVK_FORCE_POOL_RING=1` segfaults — fix that first) or one coalesced full-set-0 push per draw.
- **Compute UBO reads all zeros** when the shader also declares storage images → MoltenVK quirk; split/rearrange bindings. → §7.4
- **Vertex attribute fetch reads zeros / GPU timeout on a tiny dummy buffer.** → §7.5

### Pipeline / draw safety
- **Particles/VFX render as opaque squares with black borders** — looked like a blend/alpha bug; was a *stale pipeline* (failed build left the old one bound, wrong shader). Skip the draw when bind fails. → §7.2
- **`MODELVIEW rlPushMatrix()/rlPopMatrix()` inside a non-swapchain framebuffer corrupts a later unrelated draw.** → §7.25

### Ring / lifecycle GPU faults
- **Device lost (GPU timeout) after heavy VFX ran a while** — ring/fence lifecycle desync, not a value bug. Fix the FIRST unchecked step (`vkAcquireNextImageKHR`). → §7.3
- **Present/readback lifecycle faults.** → §7.6
- **`VUID-…-oldLayout-01211` ×30 in the validated suite** — two independent layout-transition causes. → §7.9

### Layout / soft-depth
- **Soft particles hard-cut against geometry under `Caps.noSampledDepth`** — the shadow-copy twin of §7.1. → §7.10

### Bisection discipline
- **Invisible GPU particles — a masterclass in confounded bisection** (multiple overlapping causes; how to un-confound). → §7.7
- **False alarms not to re-chase** — things that look like rlvk bugs but aren't. → §7.8

### Android bring-up (Vulkan on NDK/Mali)
- **Swapchain rotated / mispresented on first real-hardware run.** → §7.12, §7.21, and the real cause: **recreate-on-`SUBOPTIMAL` every-frame loop** → §7.22
- **Touch coordinates misaligned independent of rendering** (`SetupFramebuffer`); the root cause was **`CORE.Window.render`/`screen` set to the wrong size for Vulkan's model** — fix is a uniform-scale letterbox, keep `CORE.Window.screen` synced. → §7.13, §7.14, §7.15, §7.16, §7.17
- **shaderc on device** (statically-linked NDK shaderc); GLES-conversion + glslang gaps were the dominant blockers. → §7.18, §7.19, §7.20
- **Android dim/garbled 2D** — rlvk pool-ring descriptor path (FIXED). → §7.23

## Architecture invariants to preserve (HANDOFF §9)
These are decisions, not accidents — don't "simplify" them away: driver quirks live behind `Caps.*` flags with a repro scenario; error paths produce nothing (never leftovers); the ring/lifecycle is the single owner of frame progression. Full list in `HANDOFF.md` §9.
