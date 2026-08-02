# Decal Rebuild Progress

Source plan: `core/docs/DECAL_SYSTEM_SPEC.md`.

## D0 — Contract freeze and regression harness

Status: started.

- Replaced the obsolete Fire-only scorch source-contract test with
  `core/tests/decal_system_test.c`.
- The test covers atomic material spawn, generic composition use, frost surface
  registration, depth-test preservation, non-quantized erosion, and generic
  shader material inputs.
- Visual captures on PC Vulkan/OpenGL and Android remain outstanding.

## D1 — Atomic API and handles

Status: complete for the public-lifecycle slice.

- `DecalSystem_AddConformalMaterialEx()` now accepts material data as part of
  the spawn operation.
- `DecalSystem_SetLastConformalMaterial()` has been removed.
- `VFX_ComposeDecal()` uses the atomic path.
- Generation-safe handles, explicit `Destroy`, `IsAlive`, and transform updates
  are available. Transform updates reject conformal stamps because their
  receiver mesh is cached at spawn; receiver reprojection belongs to a later
  projection-provider phase.

## Verification

- `bash scripts/run_core_tests.sh decal_system`: passing.
- Full headless Core run: the decal suite passes. Existing failures in
  `swept_trail_test`, `tube_frame_test`, and `volume_trail_test` concern their
  authored asset-path contracts and are outside the decal change set.
