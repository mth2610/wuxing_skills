# Context — 2026-07-05T10:06:20+07:00

## Environment Context
- Operating System: macOS
- Repository Root: `/Users/mth2610/Desktop/c_games/wuxing_skills`
- Key Files:
  - `sandbox/vfx_test.c` (VFX Prefab Tester)
  - `core/visual_composer.c` (VFX Composer logic)
  - `core/visual_composer.h` (Composer headers/configs)

## Requirements Context
- **R1**: Convert `vfx_test.c` to metric system (divide cm-based values by 100). Shooting distance reduced from 40m to 8m.
- **R2**: Scale down gravity (325.0f -> 3.25f) and noise force (20.0f -> 0.2f) in `VFX_ComposeProjectileTrail`.
- **R3**: Add 'BURST' tab in `vfx_test.c`. When active, clicking triggers `VFX_ComposeTriggerImpactBurst` with a 4-step config (Screen Distortion, Ground Decal, Point Light Flash, Radial Particle Burst).

## Rationale
- We are running in the Project pattern. We will use a standard loop of Explorer -> Worker -> Reviewer/Challenger/Auditor.
- Since we have strict guidelines not to modify code directly or run commands, we must dispatch all of these tasks to subagents.
- The E2E tests for this application are manual/visual since it's a graphical C program (VFX test sandbox), but the worker and challenger will run compilation and verify.
