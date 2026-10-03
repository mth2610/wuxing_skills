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

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-03 | Codex | Jitter and fog reconstruction | core/volumetric/shaders/volumetric_fog.fs; core/volumetric/shaders/volumetric_composite.fs; core/tests/volumetric_fog_composite_test.c | Ground-truth |
