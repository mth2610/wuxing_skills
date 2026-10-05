# Volumetric Module Landmines

Lessons learned during the implementation of volumetric fog and shadow raymarching.

### 1. Depth Texture Feedback Hazard
- **Symptom:** Black screen, NaN values, or rendering freeze on Vulkan / GLES.
- **Cause:** Reading from `SceneTargets_GetRawDepthTexture()` while drawing into the same target.
- **Rule:** Always read from `SceneTargets_GetDepthTexture()` (the linearized snapshot copy taken after `SceneTargets_End()`), never the live depth attachment.

### 2. Mali GLES Precision in Ray Jitter
- **Symptom:** Flickering vertical bands or sparkle noise on mobile Mali GPUs.
- **Cause:** Using standard `fract(sin(dot(...)) * 43758.5453)` which exceeds 16-bit mediump float range.
- **Rule:** Avoid the large sine hash. `shaders/volumetric_fog.fs` now samples immutable R8 blue-noise ranks with bounded arithmetic; the former repeated 4x4 Bayer phase produced an enlarged screen grid. Desktop captures validate the grid removal; mobile visual acceptance still requires a device capture.

### 3. Bilateral Edge Bleeding on Foreground Characters
- **Symptom:** Halo / smearing around character silhouette when volumetric fog is upscaled.
- **Cause:** Naive linear filtering interpolating far fog onto near geometry across depth discontinuities.
- **Rule:** In `shaders/volumetric_composite.fs`, reconstruct premultiplied radiance and opacity together with normalized spatial weights and smooth depth rejection. Samples at or beyond `u_depthThreshold` contribute zero; the matching center depth retains support. The wider 3x3 footprint suppresses integration grain without filtering the scene color. Guard: `core/tests/volumetric_fog_composite_test.c`; captures remain necessary to inspect silhouettes and narrow shafts.

### 4. An opacity floor must bound in-scattering in the same interval
- **Symptom:** Thick fog contributes more radiance than its premultiplied opacity permits, making dense regions too bright.
- **Cause:** `shaders/volumetric_fog.fs` accumulated the complete interval's radiance before raising transmittance to the 0.20 visibility floor. With unit illumination and interval optical thickness 2, radiance was 0.864665 while opacity was 0.8.
- **Rule:** Bound the interval transmittance by `0.20 / transmittance` before radiance accumulation. This retains density, interval lengths and the visibility floor while keeping homogeneous unit-source radiance equal to opacity. Guard: `core/tests/volumetric_fog_transport_test.c`; numeric tests cannot certify rendered appearance.

### 5. Crepuscular shafts need real blockers and normalized scattering
- **Symptom:** An open meadow carries periodic slanted light streaks and a broad golden wash despite having no matching canopy overhead.
- **Cause:** `shaders/volumetric_fog.fs` projected a synthetic sine/cosine canopy onto every fog sample, then imposed a side-scattering floor and multiplied direct radiance by five. The shader sampled only dynamic shadow casters, ignoring the cached static layer.
- **Rule:** Direct in-scattering is sun color multiplied by authored god-ray intensity, the normalized Henyey-Greenstein phase, and the minimum visibility of enabled dynamic and cached-static shadow maps. Outside either map's coverage remains unoccluded. Ambient radiance stays separate; neither shadow visibility nor phase changes extinction density. `volumetric_fog.c` binds both shadow maps inside the active raymarch shader and disables unavailable layers. Guard: `core/tests/volumetric_fog_lighting_test.c`; rendered shadow orientation and visible shafts require matched captures.

### 6. Public fog shapes and shader wire ids have different orders
- **Symptom:** A requested spherical local mist behaves like a box; requested box mist is rounded.
- **Cause:** `environment/environment_system.h` defines sphere=0, box=1, cylinder=2, while `shaders/volumetric_fog.fs` dispatches box=0, sphere=1, cylinder=2. Casting the public enum directly into both transient and permanent upload packets swaps two shapes.
- **Rule:** Use the explicit enum-to-wire mapping in `volumetric_fog_volume.inl` for both upload branches. Keep the public enum stable; unrecognized values retain the shader's box fallback. Guard: `core/tests/volumetric_fog_shape_upload_test.c` executes the production packet constructor and checks upload wiring. It cannot validate rendered shape boundaries.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-05 | Codex | Local fog shape encoding | core/volumetric/volumetric_fog_volume.inl; core/tests/volumetric_fog_shape_upload_test.c; environment/environment_system.h | Ground-truth |
| 2026-10-03 | Codex | Jitter and fog reconstruction | core/volumetric/shaders/volumetric_fog.fs; core/volumetric/shaders/volumetric_composite.fs; core/tests/volumetric_fog_composite_test.c | Ground-truth |
| 2026-10-04 | Codex | Fog radiance at visibility floor | core/volumetric/shaders/volumetric_fog.fs; core/tests/volumetric_fog_transport_test.c | Ground-truth |
| 2026-10-04 | Codex | Physical shaft illumination | core/volumetric/shaders/volumetric_fog.fs; core/volumetric/volumetric_fog.c; core/tests/volumetric_fog_lighting_test.c | Ground-truth |
