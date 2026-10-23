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

---

### 7. Chromatic aberration border wrap bleeding
- **Symptom:** 1-pixel color lines along screen borders when chromatic aberration or radial blur is active.
- **Cause:** `mainRenderTex` defaulted to `TEXTURE_WRAP_REPEAT`, causing samples slightly outside $[0, 1]$ to sample the opposite screen edge.
- **Rule:** Always explicitly set `SetTextureWrap(mainRenderTex.texture, TEXTURE_WRAP_CLAMP)` upon creation.

---

### 8. Screen black/grey-out during `PostFX_SetMonochrome`
- **Symptom:** Calling `PostFX_SetMonochrome` turns the screen completely dark or 50% grey when the caller had `colorGradeEnabled = false`.
- **Cause:** PostFX enabled the color grade shader branch but passed uninitialized zero contrast and zero `colorTint`.
- **Rule:** Fallback `contrast = 1.0f` and `colorTint = (1, 1, 1)` whenever color grading is inactive but monochrome overlay is engaged.

---

### 9. GPU FBO and Shader leak on repeated `PostFX_Init`
- **Symptom:** VRAM consumption climbs when switching maps or resizing windows.
- **Cause:** `PostFX_Init()` reallocated all render textures without releasing existing ones if `mainRenderTex.id != 0`.
- **Rule:** Always check `if (mainRenderTex.id != 0) PostFX_Unload();` at the beginning of `PostFX_Init()`.

---

### 10. Adaptive render scale: screen-space passes must ask the SCENE TARGET for its size
- **Symptom:** after the scene chain started rendering at a scale below 1.0, fog was stretched and
  offset, the gas/liquid composites covered only the top-left corner of the frame, decals and
  metaballs appeared shifted, and skill shaders' screen-space effects landed in the wrong place.
  The shadow capture restored a window-sized viewport too.
- **Cause:** those passes drew a full-screen quad into the **scene target** (or fed a
  screen-space uniform) using `GetRenderWidth()/GetScreenWidth()`. That was only ever correct
  because the scene target happened to be the window size. The scene chain now renders at
  `scale * display` and the post composite upscales, so the two sizes diverge.
- **Rule:** anything that draws **into** the scene target, or does screen-space math for a shader
  that rasterises into it, must use `SceneTargets_GetWidth()/GetHeight()`. Anything that draws
  into the post chain uses `PostFX_GetWidth()/GetHeight()` (equal to the scene size) or the
  source texture's own `width/height`. Audit list fixed when the scale was introduced:
  `environment/volumetric/volumetric_fog.c` (both composites), `core/gas/gas_system.c`,
  `core/liquid/liquid_surface.c` (composite dest + `sceneTexel`), `core/metaball_fx.c`,
  `core/liquid/liquid_metaball3d.c` (`u_resolution`), `skills/skill_manager.c` (`u_resolution`),
  `post_process/screen_distort.c` (identity copy + mesh resolution). Aspect ratios may keep
  reading the window — they are scale-invariant.

---

### 11. A render-scale step must rebuild textures only — never reload shaders
- **Symptom:** the frame hitches by several milliseconds exactly when the adaptive scale steps,
  which is the one moment the frame is already over budget.
- **Cause:** `SceneTargets_Init` / `PostFX_Init` do both: they allocate the sized targets (scene
  HDR, depth, soft-depth, bg-luma, main HDR copy, bloom pyramid, LDR AA source) **and** load
  shaders plus 1×1 exposure targets. Re-running Init for a resize recompiles every shader.
- **Rule:** a scale change goes through `SceneTargets_Resize` / `PostFX_Resize`, which only
  destroy and recreate the size-dependent textures. Shader programs, uniform locations, the LUT
  and the 1×1 exposure ping-pong survive.

---

### 12. Prove the scale-1.0 path is unchanged before shipping the scale
- **Symptom:** "it looks the same" hides a real regression when the adaptive path silently
  changes the sharpened output at 1.0.
- **Cause:** the FXAA pass now applies a luma unsharp mask; if its amount were a constant it
  would sharpen even an unscaled frame and the pre-scale look would be gone.
- **Rule:** the sharpen amount is derived from the scale (`0.40 * (1/scale - 1)`, exactly `0.0`
  at 1.0, capped at 0.25) and `postfx_sharpen` overrides it. Verify with the capture harness:
  cross-build diff at 1.0 was `mean_abs 0.68 / px>24 0.06 %` against a same-build floor of
  `0.48`, i.e. inside capture noise; a 0.85 capture differs at `mean_abs 3.6 / px>8 18 %`,
  which is the intended softness trade.
