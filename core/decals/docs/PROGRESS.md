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

## D2 — Data-driven material library

Status: initial migration complete.

- Canonical policy data lives in `core/decals/decal_materials.json` and is
  generated into `decal_materials.generated.inl`.
- `DecalMaterialDesc` owns semantic surface selection, tint policy, lifecycle
  and radius curves, opacity, and emissive controls.
- `VFX_ElementMaterial` selects a decal material ID; Fire, Ice, and the shared
  impact policy are migrated.
- `VFX_ComposeDecal()` no longer branches on Fire or Ice to select renderer
  behavior.
- Future D2 work: expand the material schema with receiver/feature policy once
  D3 introduces render buckets.

## D3 — Renderer queue and state ownership

Status: initial state-correctness slice complete.

- `DecalSystem_Draw()` builds a fixed-capacity queue of drawable decals before
  issuing world passes; no allocation is introduced.
- Queue order groups compatible conformal decals by pass state, blend mode,
  flow mode, and texture.
- Both decal passes now use one flushed world-pass helper: depth testing stays
  enabled, depth writes are disabled for transparent decals, and the prior
  depth-mask/blend state is restored at pass end.
- The conformal emissive pass is not opened unless at least one queued stamp
  has non-zero emissive intensity and alpha.
- CPU frustum culling, LOD/budget admission, and per-material uniform bucketing
  remain later D3/D4/D5 work.

## Verification

- `bash scripts/run_core_tests.sh decal_system`: passing.
- Full headless Core run: the decal suite passes. Existing failures in
  `swept_trail_test`, `tube_frame_test`, and `volume_trail_test` concern their
  authored asset-path contracts and are outside the decal change set.
