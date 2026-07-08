#ifndef MAP_TOOLKIT_PROP_LIT_H
#define MAP_TOOLKIT_PROP_LIT_H

#include "raylib.h"

// prop_lit: shared lit-material shader for map terrain/props (CORE_ISSUES.md
// Item 36). Unlike every other shader in core/shaders/ (VFX-only, bound via
// skill_manager.c's SkillManager_BeginShader/EndShader wrapped around
// immediate-mode draws), prop_lit is designed to be assigned to a raylib
// Material and drawn with the standard DrawModel()/DrawModelEx() pipeline —
// one Material per prop type, many draw calls (MAP_API.md's "load once,
// draw many" convention).
//
// Usage (map Init, once per prop type):
//   Material rockMat = PropLit_MakeMaterial(diffuseTex, normalTex, roughnessTex);
//   model.materials[0] = rockMat;
//
// Usage (map Draw, once per frame before drawing any prop_lit prop):
//   PropLit_UpdateLighting();
//   DrawModel(model, pos, 1.0, WHITE);                    // white/neutral texture, no recolor
//   DrawModelEx(model2, pos2, axis, angle, scale, tint);  // tint recolors via colDiffuse
//
// Never call UnloadMaterial()/UnloadShader() on the result — the shader is
// cached by ResourceManager_LoadShader() and shared across every prop type;
// unloading it breaks every other prop using this material.
//
// Fog is NOT implemented by this shader (CORE_ISSUES.md Item 36 flags
// EnvFogConfig as unused-by-any-shader today — an open design question,
// out of scope for this item).
//
// Procedural meshes need GenMeshTangents(&mesh) called before
// LoadModelFromMesh(), or the normal map reads as flat (all-zero tangent
// attribute) — glTF-imported models usually already ship tangents, verify
// rather than assume.

// Lazy-loads (and caches, via ResourceManager_LoadShader) the shared
// prop_lit.vs/.fs shader. Safe to call every frame — cheap cache lookup
// after the first call.
Shader PropLit_GetShader(void);

// Builds a raylib Material bound to the shared prop_lit shader, with
// diffuse/normal/roughness assigned to their respective map slots. Call
// once per prop type (e.g. in a map's Init), not per draw call/instance.
Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal, Texture2D roughness);

// Pushes the current Environment_Get{SunDirection,SunColor,AmbientColor}()
// values, plus the camera position, into the shared prop_lit shader as
// uniforms. DrawModel()/DrawModelEx() do NOT auto-bind these — that's a
// SkillManager_BeginShader-only convention that never runs for a plain
// DrawModel() call — so call this once per frame before drawing any
// prop_lit material, so lighting responds to
// Environment_SetSunColor()/SetAmbientColor() changes (e.g. a day/night
// cycle).
void PropLit_UpdateLighting(void);

#endif // MAP_TOOLKIT_PROP_LIT_H
