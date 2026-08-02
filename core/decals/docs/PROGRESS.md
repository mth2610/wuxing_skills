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
- CPU frustum culling, LOD admission, and material bucketing are now tracked in
  D4/D5 below; PC Vulkan/OpenGL and Android visual captures remain required.

## D4 — CPU culling, LOD, priority, and coverage

Status: conservative frustum-admission slice complete.

- `DecalSystem_SetCamera()` retains the current `Camera3D` for queue admission.
- The queue rejects only decals whose conservative bounding sphere is clearly
  behind or outside the perspective/orthographic side planes. With no valid
  camera basis, decals remain visible rather than risking a false rejection.
- `DecalSystem_GetRenderStats()` exposes active, visible, and CPU-culled counts
  from the latest queue build without exposing decal storage.
- Projected-size LOD uses the current camera: stamps below 12 pixels are
  rejected, the 12–40 pixel tier uses a reduced conformal mesh and base pass
  only, and the 40–96 pixel tier uses a reduced mesh with emissive allowed.
- Material descriptors now supply a 0–255 priority. At the independent
  conformal-stamp cap, an incoming lower-priority stamp is rejected; otherwise
  the lowest-priority, shortest-lived stamp is replaced.
- Queue records estimated base/emissive screen coverage for tuning. Base decals
  are never rejected by this telemetry: normal combat may legitimately contain
  100–200 simultaneous marks. The 8% emissive budget only suppresses extra
  emissive passes; those decals still render their base pass.
- Material descriptors supply a conservative maximum draw distance; the queue
  keeps a decal while any part of its bounding sphere remains in range.
- At full global-pool capacity, spawn eviction prefers decals already culled by
  camera/distance admission, then the lowest priority and shortest lifetime.
  An incoming lower-priority decal is safely rejected instead of evicting a
  more important live decal.

## D5 — Batch and mobile optimization

Status: renderer optimization slice complete; backend capture validation pending.

- Per-stamp erosion no longer uses `SetShaderValue()`: the material shader reads
  erosion from vertex-color red and opacity from vertex-color alpha.
- Base/emissive tint, threshold, and intensity are applied only when the
  conformal material/tint bucket changes, inside the active shader scope.
- Adjacent decals with the same material bucket and texture now share one rlgl
  triangle submission; vertex color keeps their erosion/opacity independent.
- Legacy flow quads pack speed, strength, and glow in their otherwise-unused
  normal attribute; the flow shader receives one frame-time uniform per pass,
  not four uniforms per decal.
- `DecalRenderStats` records legacy/conformal submission estimates plus material
  and texture switches for backend captures and mobile tuning.
- Remaining D5 work is measurement on PC OpenGL/Vulkan and Android Vulkan, then
  tuning the data values from captures. Bounded forward projection is deferred
  to D6 because it is a new feature, not a prerequisite for ground stamps.
- This preserves the one-sampler shader interface and avoids the rlvk
  per-instance UBO pressure pattern. Instance-buffer batching and capture-based
  backend measurements remain outstanding.

## Verification

- `bash scripts/run_core_tests.sh decal_system`: passing.
- Full headless Core run: the decal suite passes. Existing failures in
  `swept_trail_test`, `tube_frame_test`, and `volume_trail_test` concern their
  authored asset-path contracts and are outside the decal change set.
