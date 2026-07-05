# Plan — 2026-07-05T10:06:10+07:00

## Objective
Convert sandbox/vfx_test.c to the metric system, adjust projectile forces in visual_composer.c, and implement a 'BURST' tab test in vfx_test.c.

## Concrete Steps

### Phase 1: Planning and Setup
1. [x] Initialize BRIEFING.md and progress.md.
2. [x] Write plan.md, context.md, and PROJECT.md.
3. [ ] Start safety timers and heartbeat cron.

### Phase 2: Exploration & Analysis
4. [ ] Dispatch `teamwork_preview_explorer` to:
   - Identify all instances in `sandbox/vfx_test.c` that need conversion from cm to m (light radius, decal scale, particle radius, trail ribbon length/thickness, spawn offsets, vortex/vector field forces).
   - Verify `VFX_ComposeProjectileTrail` force parameters in `visual_composer.c` (FORCE_GRAVITY_DIR, FORCE_NOISE_PERLIN) and where they are defined.
   - Investigate how tabs are structured and handled in `vfx_test.c` (how they are rendered, how mouse clicks are handled, and how `VFX_ComposeTriggerImpactBurst` should be called with an `ImpactBurstConfig`).
5. [ ] Synthesize explorer findings into a clear implementation guide.

### Phase 3: Implementation
6. [ ] Dispatch `teamwork_preview_worker` to:
   - Implement conversion of `sandbox/vfx_test.c` to metric system (R1).
   - Adjust projectile forces in `visual_composer.c` (R2).
   - Implement the 'BURST' test tab and call `VFX_ComposeTriggerImpactBurst` with a 4-step config on click (R3).
   - Compile the project (`make`) to verify there are no compilation errors.
   - Confirm it runs successfully.

### Phase 4: Review and Verification
7. [ ] Dispatch `teamwork_preview_reviewer` to check:
   - Code correctness, formatting, and safety.
   - Metric conversions are accurate and consistent.
   - No batching hazards or memory leaks.
8. [ ] Dispatch `teamwork_preview_challenger` to run verification and check:
   - Behavior of VFX in the metric system (scale, particle speed, projectile trajectory).
   - 'BURST' tab triggers all 4 effects correctly.
9. [ ] Dispatch `teamwork_preview_auditor` to verify:
   - No hardcoded test results, no dummy implementations.
   - Genuine implementation of the requested features.
   - Forensic audit verdict is CLEAN.

### Phase 5: Handoff and Completion
10. [ ] Synthesize all results.
11. [ ] Write final `handoff.md` and report completion to parent.
