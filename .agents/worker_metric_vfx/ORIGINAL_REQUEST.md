## 2026-07-05T03:07:32Z

You are teamwork_preview_worker.
Your working directory is: /Users/mth2610/Desktop/c_games/wuxing_skills/.agents/worker_metric_vfx

Tasks:
1. Implement the metric conversions in `sandbox/vfx_test.c` (R1) based on the following specific replacements:
   - Line 79 (or near): change `ScreenDistort_Add(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);` to `ScreenDistort_Add(playerPos, 1.2f, 0.8f, 1.2f, 2.5f);`
   - Line 81 (or near): change `VFXLight_Spawn(playerPos, ..., 150.0f, 9999.0f, ...);` to `VFXLight_Spawn(playerPos, ..., 1.5f, 9999.0f, ...);`
   - Line 82 (or near): change `DecalSystem_Add(..., 40.0f, ...);` to `DecalSystem_Add(..., 0.4f, ...);`
   - Line 107 (or near): change `deathChildConfig.radius = 25.0f;` to `deathChildConfig.radius = 0.25f;`
   - Line 116 (or near): change `liveChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};` to `liveChildConfig.velocity = (Vector3){0.0f, 0.1f, 0.0f};`
   - Line 119 (or near): change `liveChildConfig.radius = 30.0f;` to `liveChildConfig.radius = 0.3f;`
   - Line 129 (or near): change `Vector3Add(playerPos, (Vector3){-60.0f, 15.0f, 0.0f});` to `Vector3Add(playerPos, (Vector3){-0.6f, 0.15f, 0.0f});`
   - Line 130 (or near): change `motherConfig.velocity = (Vector3){120.0f, 0.0f, 0.0f};` to `motherConfig.velocity = (Vector3){1.2f, 0.0f, 0.0f};`
   - Line 131 (or near): change `motherConfig.radius = 40.0f;` to `motherConfig.radius = 0.4f;`
   - Line 145 (or near): change `tConfig.pos = Vector3Add(playerPos, (Vector3){75.0f, 30.0f, 0.0f});` to `tConfig.pos = Vector3Add(playerPos, (Vector3){0.75f, 0.3f, 0.0f});`
   - Line 146 (or near): change `tConfig.vel = (Vector3){220.0f, 0.0f, 0.0f};` to `tConfig.vel = (Vector3){2.2f, 0.0f, 0.0f};`
   - Line 147 (or near): change `tConfig.len = 50.0f;` to `tConfig.len = 0.5f;`
   - Line 148 (or near): change `tConfig.thick = 6.0f;` to `tConfig.thick = 0.06f;`
   - Line 179 (or near): change `vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` to `vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});`
   - Line 181 (or near): change `vortex.strength = 400.0f;` to `vortex.strength = 4.0f;`
   - Line 186 (or near): change `Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` to `Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});`
   - Line 191 (or near): change `cfg.velocity = (Vector3){cosf(ang) * 150.0f, 0.0f, sinf(ang) * 150.0f};` to `cfg.velocity = (Vector3){cosf(ang) * 1.5f, 0.0f, sinf(ang) * 1.5f};`
   - Line 194 (or near): change `cfg.radius = 6.0f;` to `cfg.radius = 0.06f;`
   - Line 236 (or near): change `vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` to `vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});`
   - Line 237 (or near): change `vf.direction = (Vector3){300.0f, 0.0f, 300.0f};` to `vf.direction = (Vector3){3.0f, 0.0f, 3.0f};`
   - Line 238 (or near): change `vf.strength = 250.0f;` to `vf.strength = 2.5f;`
   - Line 245 (or near): change `Vector3Add(playerPos, (Vector3){-250.0f, 40.0f, 0.0f});` to `Vector3Add(playerPos, (Vector3){-2.5f, 0.4f, 0.0f});`
   - Line 249 (or near): change `spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80)});` to `spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80) / 100.0f});`
   - Line 253 (or near): change `cfg.radius = 8.0f;` to `cfg.radius = 0.08f;`
   - Line 338 (or near): change `Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){40.0f, 0.0f, 0.0f});` to `Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){8.0f, 0.0f, 0.0f});`

2. Implement projectile force scaling adjustments in `core/composition/visual_composer.c` (R2):
   - Line 196 (or near): change `.strength = 325.0f` to `.strength = 3.25f` for `FORCE_GRAVITY_DIR`.
   - Line 197 (or near): change `.strength = 20.0f` to `.strength = 0.2f` for `FORCE_NOISE_PERLIN`.

3. Implement the new 'BURST' tab test in `sandbox/vfx_test.c` (R3):
   - Add `TEST_CAT_BURST` to the `PrefabTestCategory` enum before `TEST_CAT_COUNT`.
   - Update `maxIdx` and `names` check logic for grid display to include `s_testCategory == TEST_CAT_BURST`.
   - Render tab "BURST" in `VFXTest_DrawHUD`.
   - Handle clicks on the viewport when tab "BURST" is active by setting up `ImpactBurstConfig` with the appropriate 4-step config (Screen Distortion, Ground Decal, Point Light Flash, and Radial Particle Burst) based on the selected element index (0 to 7), and invoking `VFX_ComposeTriggerImpactBurst`. Use `proposed_burst_tab.patch` in `/Users/mth2610/Desktop/c_games/wuxing_skills/.agents/explorer_metric_vfx_3/` as a reference.

4. Run the build command (`make`) at the root directory of the workspace to verify there are no compilation errors.

5. Write your implementation report to `/Users/mth2610/Desktop/c_games/wuxing_skills/.agents/worker_metric_vfx/handoff.md`.
