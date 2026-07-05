# Handoff Report — Metric VFX Force Adjustments

## 1. Observation
* **Target File & Path:** `core/composition/visual_composer.c` (Note: The prompt references `core/visual_composer.c`, but the file has been moved to the feature-first subfolder `core/composition/` in the actual workspace).
* **Target Function:** `VFX_ComposeProjectileTrail` starting at line 187.
* **Exact Code Snippets:**
  - Line 196:
    ```c
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 325.0f });
    ```
  - Line 197:
    ```c
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_NOISE_PERLIN, .strength = 20.0f, .noiseScale = 0.08f, .noiseSpeed = 2.0f });
    ```
* **Definitions of Constants:**
  - `FORCE_GRAVITY_DIR` and `FORCE_NOISE_PERLIN` are enum elements of `ForceType` defined in `core/force_field.h` (Lines 27 and 31).
  - Corresponding GLSL macros are defined in `compute/shaders/gpu_particles.comp` (Lines 50 and 54) to synchronize CPU and GPU particle physics execution:
    ```glsl
    #define FT_GRAVITY_DIR   0
    #define FT_NOISE_PERLIN  4
    ```
* **Scope Reference:** Per `.agents/orchestrator/PROJECT.md`:
  - Line 18: `VFX_ComposeProjectileTrail: Composes projectile VFX. Needs updated forces internally.`
  - Line 20: `Coordinates, sizes, and velocities passed between the testbed and the composer are now strictly in meters (1.0f = 1 meter).`

## 2. Logic Chain
1. Per the architecture specified in `PROJECT.md`, the physics coordinate system is moving to a real-meter scale where `1.0f` = 1 meter, and reference gravity is `9.81f` m/s² (as documented in `CLAUDE.md`).
2. The current values in `VFX_ComposeProjectileTrail` are scaled under the old system (where `1.0f` = 1 cm):
   - `.strength = 325.0f` for `FORCE_GRAVITY_DIR` (equivalent to 325 cm/s², or 3.25 m/s²).
   - `.strength = 20.0f` for `FORCE_NOISE_PERLIN` (equivalent to 20 cm/s², or 0.2 m/s²).
3. To align these parameters with the new meter scale (R2 changes), they must be scaled down by a factor of 100:
   - `FORCE_GRAVITY_DIR` strength must go from `325.0f` to `3.25f`.
   - `FORCE_NOISE_PERLIN` strength must go from `20.0f` to `0.2f`.
4. These changes should be implemented directly in `core/composition/visual_composer.c` by updating the literals on lines 196 and 197.

## 3. Caveats
* This is a read-only investigation. No code changes have been applied to `core/composition/visual_composer.c` or other source files.
* Other instances of `FORCE_GRAVITY_DIR` and `FORCE_NOISE_PERLIN` in presets (`vfx_presets.c`), helper files (`skill_helper.c`), and specific element skills (e.g. `water_sphere_skill.c`, `tube_skill.c`, etc.) are outside the direct scope of Milestone 1/2 for `VFX_ComposeProjectileTrail` adjustments and have not been modified.

## 4. Conclusion
* The target lines for adjustments are:
  - `core/composition/visual_composer.c:196`: change `.strength = 325.0f` to `.strength = 3.25f`.
  - `core/composition/visual_composer.c:197`: change `.strength = 20.0f` to `.strength = 0.2f`.
* The constants are defined in `core/force_field.h` and implemented in both CPU evaluation (`core/force_field.c`) and GPU compute shader (`compute/shaders/gpu_particles.comp`).

## 5. Verification Method
1. Inspect the modified file `/Users/mth2610/Desktop/c_games/wuxing_skills/core/composition/visual_composer.c` at lines 196-197 to confirm the strength parameter edits.
2. Compile the project using the workspace build command `make` (from the root directory) to ensure there are no compilation errors.
3. Launch the testbed using `./wuxing` and check the projectile trail visual behavior (compare it against the expected arc/trajectory and check that it is visually equivalent to the original but working under the meter scale).
