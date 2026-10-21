# Post-Processing Module Landmines

Debugging lessons learned in post-processing. Format: **Symptom → Cause → Rule**.

---

### 1. Blend-state toggle around batched draws without batch flush
- **Symptom:** Bloom prefilter or composite blit outputs over-brightened clipped white regions or unblended alpha.
- **Cause:** `rlDisableColorBlend(); DrawTexturePro(...); rlEnableColorBlend();` restores blending before the queued batch is submitted to the GPU.
- **Rule:** Always call `rlDrawRenderBatchActive()` before modifying or restoring blend state. (See `ENGINE_LANDMINES.md` #16).

---

### 2. RT $\to$ RT vertical flip convention in FXAA pass
- **Symptom:** Upside-down final render or distorted aspect when toggling FXAA.
- **Cause:** Raylib render texture convention maps render texture Y coordinates inverted relative to swapchain coordinates.
- **Rule:** The source rectangle for full-frame resolve must specify negative height `-(float)ldrTex.texture.height`.

---

### 3. Applying LUT before Tone Mapping
- **Symptom:** Severe highlight clipping, solarized bright halos, and loss of bloom detail.
- **Cause:** A 3D LUT is authored in display-referred $[0.0, 1.0]$ space. Sampling it with linear HDR values $(> 1.0)$ clamps all bright emissives to the highest slice.
- **Rule:** Always apply 3D LUT grading after ACES tone mapping in `post_process.fs`.

---

### 4. FXAA executed on linear HDR values
- **Symptom:** Smudged bright highlights while geometric silhouette edges remain jagged.
- **Cause:** FXAA calculates edge contrast using perceptual luma weights (`vec3(0.299, 0.587, 0.114)`). On linear HDR, the dynamic range dwarfs edge contrast.
- **Rule:** Execute FXAA strictly after ACES tone mapping in the LDR resolve target.

---

### 5. Black screen when skipping distortion copy via `PostFX_UseDirectSource`
- **Symptom:** Game screen stays pitch black when no shockwaves are active.
- **Cause:** `PostFX_Begin()` / `PostFX_End()` automatically restores the default framebuffer and viewport via `EndTextureMode()`. Bypassing them leaves the previous FBO bound.
- **Rule:** When calling `PostFX_UseDirectSource()`, explicitly issue `rlDrawRenderBatchActive()`, `rlDisableFramebuffer()`, and `rlViewport(0, 0, GetRenderWidth(), GetRenderHeight())`.

---

### 6. Thin HDR emitters disappearing during bloom downsampling
- **Symptom:** One-pixel hot features (lightning bolts, thin shield rims) fail to bloom.
- **Cause:** Standard box-filter downsampling averages a $4 \times 4$ cell into a single sample, dropping a $4.5$ HDR peak below the $1.25$ threshold.
- **Rule:** Use a brightest-sample prefilter in `bloom_bright.fs` to preserve maximum HDR energy across the $4 \times 4$ source footprint.
