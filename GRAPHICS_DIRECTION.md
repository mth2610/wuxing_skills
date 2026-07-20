# GRAPHICS DIRECTION — Overall visual direction

> Locked 2026-07-13. This is a **major pivot** from the original art direction
> (`nguhanhtyvo_kehoach.md`: low-poly + night + fog to HIDE a lack of detail & optimize for
> Android). New direction: **eye-catching, stylized-realism wuxia** — still runs on weak machines /
> Android GLES. `WUXING_ART_DIRECTION.md` is unchanged (it covers skill VFX); this document covers
> the **surface render + atmosphere + post-processing** of the overall scene.

## 1. Target look: Thiên Nhai Minh Nguyệt Đao (Moonlight Blade)

Stylized-**realism** wuxia — NOT cel/anime:
- Characters at real proportions, skin/cloth/metal **have material identity** but are idealized
  (clean, smooth, beautifully lit). NO stepped cel-ramp, NO anime-style outline.
- The beauty comes from **material + atmosphere + cinematic post-processing**, NOT physical
  accuracy. This is exactly why it can run on weak hardware.
- Runs on low-end machines by **avoiding real-time shadows + GI** (the most expensive things).
  Fake shadows + baked/fake lighting are good enough — confirmed as an intentional choice by the
  user.

## 2. Platform constraint: Android GLES (unchanged)

- Shaders must run on GLES (already has the 2-branch architecture, see memory
  `android-shader-pipeline`: matModel identity, mediump/highp precision, f-suffix literals...).
  Every new shader MUST have a GLES branch.
- Forward rendering, few lights (1 sun + ambient + fill). NO heavy deferred MRT.
- Selective post-processing: bloom (already has a mip-chain), tone mapping, color grading, fog, and
  fake god-rays are all cheap. AVOID: heavy bokeh DOF, SSAO, real volumetric raymarching (keep those
  PC-only or drop them).
- Extra budget available since the scene is small (1 map, ≤8 players, simple minions) → spend it on
  pixel quality, not quantity.

## 3. Render state survey (as of 07/13)

- **Characters: stylized surface shader ✅ (G2)** — replaced raylib's default UNLIT shader with
  `core/surface_material` + `surface_lit.vs/.fs`: half-Lambert + Blinn sheen + cool Fresnel rim
  (moonlit edge) + fog, lit by environment_system. Applied via `SurfaceMaterial_Apply` on model
  load. Characters now have real volume/shading, no longer "mannequins". (Enemy/dummy characters
  still use the stick-figure `DrawCharacter3D` until assets arrive.)
- **Tone mapping + HDR ✅** — the pipeline is now **true HDR**: the scene buffer
  (`core/screen_distort.c` renderTex) + bloom pyramid + composite all use 16-bit half-float
  (R16G16B16A16), so additive/emissive values stay above 1.0 until ACES filmic compresses HDR→LDR
  at composite. Has a probe/fallback to RGBA8 for GLES2 (`ScreenDistort_IsHDR()` is the deciding
  flag, `PostFX_IsHDR()` follows it). Bloom already has a mip-chain (bright/downsample/upsample/
  blur) — a solid foundation.
- Have: the post-fx chain (bloom, chromatic aberration, vignette, color grade), compute particles,
  a strong VFX composition system, environment (sun + ambient + fog + time-of-day), fake blob
  shadows. ~60% reusable as-is.

## 4. Roadmap (the "G" rounds) — ordered by impact/effort ratio

| # | Round | Content | Needs user assets? |
|---|---|---|---|
| **G1 ✅** | HDR pipeline + tone mapping | Scene/bloom/composite → RGBA16F float (true HDR); ACES filmic HDR→LDR; GLES2 probe+fallback | No |
| **G2** | Character/environment surface shader | Smooth half-Lambert + Blinn specular + normal map + rim + fog. Replaces the default shader. GLES 2-branch | No (looks good on existing models immediately; normal maps are a further upgrade) |
| **G3** | Wuxia atmosphere | Upgraded height fog + god-rays/light-shafts (billboard, cheap on GLES) + floating dust/embers + aerial perspective | No |
| **G4** | Higher-quality fake shadows | Soft gradient blob / simple projected shadow replacing the hard blob | No |
| **G5** | Cinematic color grading + stylized water | Wuxia LUT (cold moonlight/amber); water with fresnel+foam+distortion | No |
| **G6** | Integrate quality assets | Wuxia character models + environment props (Synty/marketplace) through the new shaders | **Yes** |

G1-G5 are engine/shader work (Claude does these, no need to wait on assets). G6 is when your assets
come in and "bloom" thanks to the shader foundation already built.

## 5. Asset pipeline (recommendation)

- **Characters**: realistically-proportioned wuxia-style models — ArtStation/CGTrader/Sketchfab
  ("wuxia/xianxia/chinese warrior"), Synty POLYGON Oriental. **Prefer models with a normal map** so
  G2 pays off fully. Rig/animate via Mixamo. Export as GLB.
- **Environment**: Synty POLYGON (Fantasy/Samurai/Oriental), Quaternius/Kenney (free) for stylized
  props.
- **Ramp/LUT/gradient**: author in Blender/GIMP.
- **Central tool**: Blender (free).
- Do NOT use VRoid (that's anime style — wrong direction).

## 6. Coding principles (every new shader)

1. Always have both GLES and PC branches (Rules A-E, memory `android-shader-pipeline`).
2. Tone mapping is the LAST step of the chain, after bloom, before UI color-grade.
3. New material shaders go through `core/material/` or their own shader file — do NOT hard-code
   colors (use textures/uniforms).
4. Every atmosphere effect must be cheap on GLES — fake it first, do it for real later (PC-only if
   ever).
5. Preserve gameplay readability: don't let post-processing swallow skill/team indicators.
