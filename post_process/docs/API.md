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
