# Volumetric Fog & Atmospheric God-Rays API

Public interface of `core/volumetric/volumetric_fog.h`. Provides screen-space downscaled volumetric raymarching, directional shadow God-rays (crepuscular rays), and depth-aware bilateral upsampling into the HDR scene buffer.

## 1. Quick Reference

| Signature | Description |
|---|---|
| `void VolumetricFog_Init(int width, int height)` | Allocates downscaled HDR render target and compiles shaders |
| `void VolumetricFog_Unload(void)` | Frees textures and shaders |
| `void VolumetricFog_Resize(int width, int height)` | Rebuilds low-res target on window resize |
| `void VolumetricFog_Render(Camera3D camera)` | Runs low-res raymarch and bilateral composite passes |
| `bool VolumetricFog_IsEnabled(void)` | Query master toggle |
| `void VolumetricFog_SetEnabled(bool enabled)` | Enable/disable volumetric pass |
| `void VolumetricFog_SetGodRayIntensity(float intensity)` | Adjust brightness of sunlight shafts |
| `float VolumetricFog_GetGodRayIntensity(void)` | Query god-ray intensity multiplier |

---

## 2. Calling Convention & Pipeline Position

`VolumetricFog_Render()` must be called **after** the 3D scene opaque pass finishes and `SceneTargets_SnapshotDepth()` has recorded the scene depth, and **before** post-processing (`PostFX_Draw()`):

```c
// In main.c render loop:
SceneTargets_SnapshotDepth();
VolumetricFog_Render(camera);
PostFX_Draw(&postFXConfig);
```

Quality tier scaling:
- **`GFX_LOW` / `GFX_UNLIT`**: Pass is bypassed (0 ms cost; forward height fog in `surface_lit.fs` is used instead).
- **`GFX_MED`**: 12 raymarching steps.
- **`GFX_HIGH`**: 24 raymarching steps + Depth-Aware Bilateral filter.
