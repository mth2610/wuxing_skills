#ifndef SURFACE_MATERIAL_H
#define SURFACE_MATERIAL_H

#include "raylib.h"

// Đợt G2 — shared stylized-realism surface shader for character/prop MODELS.
// Replaces raylib's UNLIT default shader (flat "mannequin" look) with
// half-Lambert diffuse + Blinn sheen + cool Fresnel rim + distance fog, driven
// by environment_system's sun/ambient/fog. Reusable: any Model that should be
// lit this way calls SurfaceMaterial_Apply on it once after loading.
//
// Usage:
//   SurfaceMaterial_Init();                 // once, after window/GL is up
//   SurfaceMaterial_Apply(&someModel);      // once per model after loading
//   ... each frame, inside 3D pass, before drawing lit models:
//   SurfaceMaterial_UpdateFrame(camera);    // pushes sun/ambient/fog/viewPos

void   SurfaceMaterial_Init(void);
Shader SurfaceMaterial_GetShader(void);

// Assign the surface shader to every material of `model`. Safe no-op if the
// model has no materials or SurfaceMaterial_Init hasn't run.
void SurfaceMaterial_Apply(Model *model);

// Push per-frame lighting uniforms from environment_system + camera. Call once
// per frame before drawing any model the shader is applied to.
void SurfaceMaterial_UpdateFrame(Camera3D camera);

#endif // SURFACE_MATERIAL_H
