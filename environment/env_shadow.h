#ifndef ENV_SHADOW_H
#define ENV_SHADOW_H

#include "raylib.h"

// Real Shading P6 — single directional shadow map (Environment Agent owns).
// The one genuinely heavy technique in the plan: a depth-only pass from the
// sun's POV into a sampleable depth texture, PCF-filtered by surface_lit.fs
// at HIGH tier only. Fake blob shadows (Environment_DrawSmartShadow) remain
// the default everywhere else; this is an opt-in "+Shadow" layer on top.
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

// Moves and sizes the single directional-shadow coverage region. The center
// should follow the camera/player for large maps; halfExtent is clamped to a
// safe range. Focus is snapped in light space to keep shadow texels stable.
// Existing arena users need not call this.
void       EnvShadow_SetFocus(Vector3 center, float halfExtent);

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

// TEMP diagnostic (P6 bug 3) — CPU-readback of the shadow map + numeric
// projection of a reference world position. Prints, via TraceLog: stored
// depth min/max/histogram, the projected texel of `worldPos` (the caster,
// e.g. player position) and of the ground point underneath it, and the
// stored depth at both texels. One call = full numeric picture, no more
// color-guessing from screenshots.
void EnvShadow_DebugDump(Vector3 worldPos);

#endif // ENV_SHADOW_H
