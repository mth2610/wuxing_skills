# Post-Processing Module API

Documents the **Post-Processing** module (`post_process/post_fx.h` and `post_process/color_grade_lut.h`). Responsible for full-screen image operations, HDR half-float pipeline, dual-filter bloom pyramid, ACES filmic tone mapping, color grading, 3D LUT strips, FXAA, and transient screen bursts.

## 1. Quick Reference

| Function / Type | Header | Role |
|---|---|---|
| `PostFXConfig` | `post_process/post_fx.h` | Configuration struct controlling bloom, tone mapping, grading, vignette, chromatic aberration, and FXAA. |
| `PostFX_Init(width, height)` | `post_process/post_fx.h` | Initializes offscreen render targets, bloom pyramid FBOs, and compiles post-fx shaders. |
| `PostFX_IsHDR()` | `post_process/post_fx.h` | Returns true if the offscreen chain uses RGBA16F (half-float) format. |
| `PostFX_Unload()` | `post_process/post_fx.h` | Frees all render textures and shaders. |
| `PostFX_Begin()` / `PostFX_End()` | `post_process/post_fx.h` | Binds and unbinds the PostFX offscreen HDR render target. |
| `PostFX_UseDirectSource(tex)` | `post_process/post_fx.h` | Points the composite pass directly at an external texture, bypassing intermediate copies when distortion is inactive. |
| `PostFX_Draw(config)` | `post_process/post_fx.h` | Executes the complete post-fx pipeline (Bloom $\to$ Composite $\to$ FXAA) onto the active framebuffer. |
| `PostFX_SetMonochrome(intensity)` | `post_process/post_fx.h` | Overrides saturation towards greyscale (used for Cảnh Giới Thái Cực). |
| `PostFX_RadialBurst(pos, str, dur)` | `post_process/post_fx.h` | Triggers a transient screen-space burst/shockwave centered at a 3D world position. |
| `PostFX_UpdateTransient(cam, dt)` | `post_process/post_fx.h` | Decays active bursts and projects world positions to screen UV coordinates. |
| `ColorGradeLut_Init()` | `post_process/color_grade_lut.h` | Builds and binds the default neutral 16x16 strip LUT. |
| `ColorGradeLut_Load(path)` | `post_process/color_grade_lut.h` | Loads an external $256 \times 16$ 3D LUT strip from disk. |
| `ColorGradeLut_Texture()` | `post_process/color_grade_lut.h` | Returns the active 2D strip texture for shader binding. |
| `SceneTargets_GetWidth()` / `GetHeight()` | `post_process/scene_targets.h` | Size actually in use by the scene chain — the render scale may make it smaller than the window. Anything drawing **into** the scene target must use these. |
| `SceneTargets_BeginWorldRefraction()` / `EndWorldRefraction()` | `post_process/scene_targets.h` | Brackets the 3D refraction layer (shield / metaball 3D) that runs after `SceneTargets_End`: scene FBO with the **target** viewport, then the window viewport back for the 2D composite layer. |
| `SceneTargets_RequestSoftDepthRegion(rect)` | `post_process/scene_targets.h` | Arms the part of the depth buffer soft particles need this frame. `rect` is in **scene-target** pixels; build it from a world position via `SceneTargets_WindowRectToTarget()` (`GetWorldToScreen` output is window pixels). |
| `SceneTargets_WindowRectToTarget(rect)` | `post_process/scene_targets.h` | Converts a window-space rectangle to scene-target pixels (identity at scale 1.0). |
| `SceneTargets_Resize(w, h)` | `post_process/scene_targets.h` | Rebuilds the size-dependent scene buffers for a new render scale. Textures only: no shader reload. |
| `PostFX_GetWidth()` / `GetHeight()` | `post_process/post_fx.h` | Size of the post chain (main HDR copy / bloom / LDR AA source), equal to the scene size. |
| `PostFX_Resize(w, h)` | `post_process/post_fx.h` | Rebuilds the post-chain targets; shaders, LUT and uniform locations survive. |
| `RenderScale_Init(w, h)` | `post_process/render_scale.h` | Sets up the adaptive render scale (or a fixed `WUXING_RENDER_SCALE`). |
| `RenderScale_Frame(submitMs, intervalMs, w, h)` | `post_process/render_scale.h` | Once per frame, after drawing: feeds the controller. Decides on the submit time, uses the interval only as confirmation. |
| `RenderScale_TakeResize(&w, &h)` | `post_process/render_scale.h` | At the top of the frame: true when the scale changed; caller then calls the two Resize functions. |
| `RenderScale_GetScale()` / `GetWidth()` / `GetHeight()` | `post_process/render_scale.h` | Current rung (1.0 … floor), and the derived size. |

---

## 2. Data Structures

### `PostFXConfig`
Main parameter struct for configuring the post-processing stack:

```c
typedef struct {
  // Bloom
  bool bloomEnabled;
  float bloomThreshold; // HDR luma threshold (>1.0 = emissive only)
  float bloomIntensity; // Additive intensity scale
  float bloomScatter;   // Pyramid fold mix factor (default: 0.65)

  // Chromatic Aberration
  bool chromaticEnabled;
  float chromaticStrength;

  // Vignette
  bool vignetteEnabled;
  float vignetteRadius;    // Bright circle radius [0.0 .. 1.5]
  float vignetteSoftness;  // Edge falloff [0.0 .. 1.0]

  // Color Grading
  bool colorGradeEnabled;
  float contrast;          // [0.5 .. 2.0]
  float saturation;        // [0.0 .. 2.0]
  Vector3 colorTint;       // RGB tint multiplier
  Vector3 shadowTint;      // Shadow luminance tint
  Vector3 highlightTint;   // Highlight luminance tint

  // Tone Mapping
  bool  tonemapEnabled;
  float exposure;          // Pre-curve scene brightness [0.5 .. 2.0]

  // 3D Colour-grade LUT
  bool  lutEnabled;
  float lutStrength;       // Blend factor [0.0 .. 1.0]

  // Radial Blur / Transient Screen Bursts
  bool    radialBlurEnabled;
  Vector2 radialBlurCenter;   // Screen UV [0.0 .. 1.0]
  float   radialBlurStrength; // Sample offset scale
  float   radialBlurFalloff;  // UV radius for full strength

  // Anamorphic Streak Bloom
  bool  bloomStreakEnabled;
  float bloomStreakStrength; // Blend of streak vs circular tap [0.0 .. 1.0]
  float bloomStreakAngle;    // Radians (0 = horizontal)
} PostFXConfig;
```

---

## 3. Core Invariants & Calling Conventions

1. **HDR Domain Preservation:**
   - Scene rasterization and bloom pyramid levels 0–5 operate in linear HDR space using `RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16`.
   - ACES tone mapping converts HDR scene radiance into display-referred $[0.0, 1.0]$ LDR.
2. **Display-Referred Color LUT:**
   - The 3D strip LUT is applied **after** tone mapping on display-referred values. Applying a LUT to HDR scene radiance would clamp highlights.
3. **FXAA Placement:**
   - FXAA operates on perceptual luminance after tone mapping. When FXAA is enabled, the composite renders to `ldrTex` before FXAA resolves to the backbuffer.
4. **Direct Source Bypass (`PostFX_UseDirectSource`):**
   - When no distortion shockwaves are active, `PostFX_UseDirectSource()` is called instead of `PostFX_Begin()` / `PostFX_End()`, eliminating an unnecessary full-screen HDR blit (~15 MB per frame at 720p).
5. **Transient Screen Burst Trauma Model:**
   - Bursts are registered via `PostFX_RadialBurst(worldPos, strength, duration)`. Only the highest-magnitude burst is projected and active at any time, preventing competing focal points.


---

## 4. Adaptive Render Scale

The frame is fill-bound: on the target hardware (Intel iGPU through rlvk/MoltenVK at 1280x720,
HIGH) the cost tracks the number of shaded pixels almost linearly, so the scene chain renders at
`scale * display` and the post composite upscales to the swapchain with the blit that already
existed for the Android native-size mismatch.

**The rung is a per-map constant, not a per-second variable.** Every change reallocates the scene
and post chains (~35 MB at 720p) and rebuilds six framebuffers: a hitch of tens of milliseconds,
a window for the compositor to present a blank frame, and — because the frame sits next to the
16.67 ms refresh — a 1.5 ms step can flip the delivered rate between one and two refreshes. A
controller that tracks the budget therefore oscillates 60 → 30 fps and flashes the screen on each
step. Both were observed in play.

- **Ladder:** `1.00, 0.95, 0.90, 0.85, 0.80, 0.75`. Starts at **0.85** (the measured sweet spot,
  and the rung most likely kept, so the common case never resizes at all).
- **Settle:** the first ~2 s after a map/screen change are ignored — asset and effect ramp-in is
  not the steady state (a calibration in that window claimed 11.6 ms where the steady state was
  16.8 ms, and the controller then had to walk itself back down).
- **Down:** one rung after ~2 s of sustained submit > 16.0 ms (up to 4 per map, enough to walk
  to the floor). Never on a single hitch frame.
- **Up:** one rung after ~5 s of sustained submit < 10.5 ms, **once per map**. If a down-step
  follows within 15 s, the up-step is marked wrong and never retried on that map.
- **Signal:** the *submit* time of the frame (top of the frame → just before `EndDrawing`),
  excluding the vsync wait. A delivered interval saturates at one refresh under
  `SetTargetFPS(60)` and cannot distinguish a 9 ms frame from a 16.5 ms one, so it is not used.
- **Measured** (song_quao, 1280x720, HIGH, paired full-frame runs): `1.00 → 16.83 ms`,
  `0.85 → 13.78 ms` (median of 3 pairs, 3/3). Visual cost of 0.85: `mean_abs 3.6`,
  `px>8 18 %` against the frame — a bilinear-upscale softness on grass detail, partly recovered
  by the luma sharpen inside the FXAA pass (`post_process/docs/LANDMINES.md` #12).
- **World-space passes after `SceneTargets_End`** (shield / liquid-metaball 3D refraction) must be
  bracketed by `SceneTargets_BeginWorldRefraction()` / `SceneTargets_EndWorldRefraction()`:
  re-binding the scene FBO does not restore its viewport, and `EndTextureMode` left it at the
  window size. The 2D layer that follows (gas/liquid/metaball composites, volumetric fog) must
  keep the window viewport — it draws with the window ortho (`post_process/docs/LANDMINES.md` #14).
- **Knobs:** `WUXING_RENDER_SCALE` pins a fixed rung for A/B; `WUXING_RENDER_SCALE_START` changes
  the starting rung; `WUXING_RENDER_SCALE_MIN` moves the floor (default 0.80).
- **Headless captures are pinned to a fixed scale** so a capture is reproducible;
  `WUXING_RENDER_SCALE_ADAPTIVE=1` forces the controller on there for testing.
