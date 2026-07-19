#ifndef SURFACE_MATERIAL_H
#define SURFACE_MATERIAL_H

#include "raylib.h"

// Đợt G2 (+ Real Shading P1) — shared stylized-realism surface shader for
// character/prop MODELS. Replaces raylib's UNLIT default shader (flat
// "mannequin" look) with half-Lambert diffuse + hemispheric ambient + Blinn
// sheen + cool Fresnel rim + emissive + distance fog, driven by
// environment_system's sun/ambient/fog and gated by core/gfx_quality.h's
// global quality tier (see CORE_API.md §18 / REAL_SHADING_SPEC.md). Reusable:
// any Model that should be lit this way calls SurfaceMaterial_Apply on it
// once after loading.
//
// Usage:
//   SurfaceMaterial_Init();                 // once, after window/GL is up
//   SurfaceMaterial_Apply(&someModel);      // once per model after loading
//   ... each frame, inside 3D pass, before drawing lit models:
//   SurfaceMaterial_UpdateFrame(camera);    // pushes tier/sun/ambient/fog/viewPos

void   SurfaceMaterial_Init(void);
Shader SurfaceMaterial_GetShader(void);

// Assign the surface shader to every material of `model`. Safe no-op if the
// model has no materials or SurfaceMaterial_Init hasn't run.
void SurfaceMaterial_Apply(Model *model);

// Push per-frame lighting uniforms from environment_system + camera. Call once
// per frame before drawing any model the shader is applied to.
void SurfaceMaterial_UpdateFrame(Camera3D camera);

// Real Shading P3c — matcap / lit-sphere material (jade/metal/energy props).
// The shader is shared globally, so this toggles a per-call state: call
// SetMatcapActive right before DrawModel(s) that should use it, then
// ClearMatcap right after so later draws aren't affected. amount in [0,1]
// (0 = no matcap contribution, 1 = fully replaces the base shading response).
// MED tier and above only (no-op visually below MED regardless of the flag).
void SurfaceMaterial_SetMatcapActive(Texture2D matcap, float amount);
void SurfaceMaterial_ClearMatcap(void);

// Real Shading P5 — HIGH-tier-only material extras (no-op below GFX_HIGH).
// Same call-around-the-draw pattern as the matcap setters above: the shader
// is shared globally, so wrap the specific DrawModel(s) that should use it.
void SurfaceMaterial_SetNormalMapActive(Texture2D normalMap);
void SurfaceMaterial_ClearNormalMap(void);

void SurfaceMaterial_SetAniso(float anisoShininess); // hair/silk streaked highlight
void SurfaceMaterial_ClearAniso(void);

void SurfaceMaterial_SetSSS(float strength, float power); // jade/skin back-scatter, strength=0 clears
void SurfaceMaterial_ClearSSS(void);

// Real Shading P6 — swap a model's materials to the shadow depth shader for
// the EnvShadow_BeginCapture()..EndCapture() pass, then back to the lit
// shader. `model` is passed by value (Model.materials is a pointer — the
// underlying array is shared, so this affects the real model's draws).
void SurfaceMaterial_BeginShadowCast(Model model, Shader depthShader);
void SurfaceMaterial_EndShadowCast(Model model);

#endif // SURFACE_MATERIAL_H
