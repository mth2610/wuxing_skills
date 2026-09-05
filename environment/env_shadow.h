#ifndef ENV_SHADOW_H
#define ENV_SHADOW_H

#include "raylib.h"

// Real Shading P6 — two-layer directional shadows (Environment Agent owns).
// A camera-following target updates dynamic casters each frame; an optional
// world-fixed target caches static map geometry. Receivers combine both maps.
// Fake blob shadows (Environment_DrawSmartShadow) remain the low-cost fallback.
//
// Usage (once per frame, BEFORE the normal camera 3D pass):
//   if (EnvShadow_IsEnabled()) {
//       EnvShadow_BeginCapture();
//       SurfaceMaterial_BeginShadowCast(CharacterModel_GetModel(), EnvShadow_GetDepthShader());
//       ... draw the same scene geometry that should cast shadows ...
//       SurfaceMaterial_EndShadowCast(CharacterModel_GetModel());
//       EnvShadow_EndCapture();
//   }
//   ... then the normal camera pass; SurfaceMaterial_UpdateFrame() pushes
//   u_lightVP/shadowMap/u_shadowEnabled automatically from the getters below.
//
// NOT profiled on Mali. Default OFF on every platform — call
// EnvShadow_SetEnabled(true) explicitly (sandbox hotkey/options menu) after
// verifying perf headroom; per REAL_SHADING_PLAN.md do NOT ship enabled on
// Mali until profiled.

void EnvShadow_Init(void); // once, after Environment_Init + SurfaceMaterial_Init

void       EnvShadow_SetEnabled(bool enabled);
bool       EnvShadow_IsEnabled(void);   // false also when Init failed (e.g. FBO incomplete)

// Moves and sizes the dynamic directional-shadow coverage region. The center
// should follow the camera/player for large maps; halfExtent is clamped to a
// safe range. Focus is snapped in light space to keep shadow texels stable.
// Existing arena users need not call this.
// Diagnostics may override X/Z with WUXING_SHADOW_FOCUS_X and
// WUXING_SHADOW_FOCUS_Z; both variables must be present.
void       EnvShadow_SetFocus(Vector3 center, float halfExtent);
Vector3    EnvShadow_GetFocus(void);
float      EnvShadow_GetHalfExtent(void);

void       EnvShadow_BeginCapture(void); // begins the light-space depth pass
void       EnvShadow_EndCapture(void);   // ends it, restores default framebuffer/viewport
bool       EnvShadow_IsCapturing(void);  // true between Begin/EndCapture — scene draw code should
                                          // SKIP non-casters (UI/HP bars, fake shadows, wires,
                                          // outlines, decals) when this is set: a shadow map only
                                          // needs solid caster geometry, everything else is wasted
                                          // fill/geometry re-rendered into the map every frame.

Shader     EnvShadow_GetDepthShader(void); // assign to casters during BeginCapture..EndCapture
Matrix     EnvShadow_GetLightVP(void);     // combined light view*projection, for the main pass
Texture2D  EnvShadow_GetShadowMap(void);   // sampleable depth texture, for the main pass

// Optional map-owned caster hook. Called inside the dynamic light-space pass
// after its target/matrices are active and before engine-owned casters draw.
// Register at map init and clear it before unloading map resources.
typedef void (*EnvShadowMapCasterCallback)(Shader depthShader, void *userData);
void EnvShadow_SetMapCasterCallback(EnvShadowMapCasterCallback callback, void *userData);

// Cached world-scale layer for terrain and other static map casters. A map
// captures this once after its static models are built; the normal Begin/End
// pass remains camera-focused and updates dynamic casters every frame. The
// cache becomes unavailable if the directional-light vector changes, so a
// day/night transition must rebuild it at its chosen update cadence.
void       EnvShadow_BeginStaticCapture(Vector3 center, float halfExtent);
void       EnvShadow_EndStaticCapture(void);
void       EnvShadow_InvalidateStaticCache(void);
bool       EnvShadow_HasStaticCache(void);
bool       EnvShadow_NeedsStaticCapture(void);
Matrix     EnvShadow_GetStaticLightVP(void);
Texture2D  EnvShadow_GetStaticShadowMap(void);

// Set WUXING_SHADOW_DYNAMIC_VERIFY=1 for a one-shot R32F readback after the
// first completed dynamic capture. The log reports minimum depth, occupied
// texels, and projected ground-receiver coverage in both texture orientations;
// intended for automated caster validation, never normal gameplay.
//
// Manual diagnostic — CPU-readback of the shadow map + numeric
// projection of a reference world position. Prints, via TraceLog: stored
// depth min/max/histogram, the projected texel of `worldPos` (the caster,
// e.g. player position) and of the ground point underneath it, and the
// stored depth at both texels. One call = full numeric picture, no more
// color-guessing from screenshots.
void EnvShadow_DebugDump(Vector3 worldPos);

#endif // ENV_SHADOW_H
