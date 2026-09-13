# Volumetric Module Landmines

Lessons learned during the implementation of volumetric fog and shadow raymarching.

### 1. Depth Texture Feedback Hazard
- **Symptom:** Black screen, NaN values, or rendering freeze on Vulkan / GLES.
- **Cause:** Reading from `SceneTargets_GetRawDepthTexture()` while drawing into the same target.
- **Rule:** Always read from `SceneTargets_GetDepthTexture()` (the linearized snapshot copy taken after `SceneTargets_End()`), never the live depth attachment.

### 2. Mali GLES Precision in Ray Jitter
- **Symptom:** Flickering vertical bands or sparkle noise on mobile Mali GPUs.
- **Cause:** Using standard `fract(sin(dot(...)) * 43758.5453)` which exceeds 16-bit mediump float range.
- **Rule:** Use a constant 4x4 Bayer matrix lookup (`Bayer4x4()`) with integer modulo for jittering.

### 3. Bilateral Edge Bleeding on Foreground Characters
- **Symptom:** Halo / smearing around character silhouette when volumetric fog is upscaled.
- **Cause:** Naive linear filtering interpolating far fog onto near geometry across depth discontinuities.
- **Rule:** Use depth-aware bilateral weighting `1.0 / (1.0 + abs(fullDepth - lowDepth) * 2.0)` in the composite pass.
