#ifndef MAP_TOOLKIT_GRASS_MATERIAL_H
#define MAP_TOOLKIT_GRASS_MATERIAL_H

#include "raylib.h"

// grass_material: texture-blend hybrid ground material (CORE_ISSUES.md
// Item 38, supersedes the pure-procedural Item 37 version). Item 37's
// 100%-shader approach was visually reviewed against a real reference
// screenshot and rejected: smooth color-only blobs with no fine surface
// grain read as fake, and gave an illusion of drifting under camera
// motion (no fixed high-contrast detail for the eye to anchor to).
//
// New approach follows how mobile MOBA/MMORPG ground actually works
// (and matches prop_lit/Item 36's precedent): ~80% texture, ~20% shader.
// Real photo textures (grassBase, grassDetail, dirt) carry the actual
// surface detail; the shader ONLY blends layers together and adds broad
// procedural color variation via fbm2() (core/shaders/common/noise.glsl)
// — no baked mask/noise texture files needed for that part.
//
// Like prop_lit, drawn via raylib's standard Material +
// DrawModel()/DrawModelEx() pipeline — one Material per ground mesh,
// many draw calls — not SkillManager_BeginShader/immediate-mode.
//
// Usage (map Init, once per ground mesh):
//   GrassMaterialConfig cfg = {
//       .grassBase      = grassBaseTex,    // real grass photo texture
//       .grassDetail    = grassDetailTex,  // fine grayscale grain, ~0.5-centered, tileable
//       .dirt           = dirtTex,         // dirt patch photo texture
//       .baseTileSize   = 4.0f,            // world meters per grassBase/dirt tile
//       .detailTileSize = 0.6f,            // world meters per grassDetail tile (denser repeat)
//       .maskNoiseScale = 0.08f,           // world-space fbm2 frequency for dirt-through-grass mask
//       .colorVarScale  = 0.02f,           // world-space fbm2 frequency for broad tint variation
//   };
//   Material groundMat = GrassMaterial_Make(cfg);
//   model.materials[0] = groundMat;
//
// Caller is responsible for setting each texture's wrap mode to
// TEXTURE_WRAP_REPEAT before calling GrassMaterial_Make (same
// caller-responsibility convention prop_lit's callers already follow —
// this file does not call SetTextureWrap itself).
//
// Usage (map Draw, once per frame before drawing any grass_material prop):
//   GrassMaterial_UpdateLighting();
//   DrawModel(model, pos, 1.0f, WHITE);
//
// Never call UnloadMaterial()/UnloadShader() on the result — the
// shader is cached by ResourceManager_LoadShader() and shared across
// every grass_material user; unloading it breaks every other user of
// this material.
//
// Not a replacement for prop_lit — prop_lit remains the right tool for
// anything that should read as a distinct textured surface (rock,
// stone path, generic props). This is specifically for ground that
// blends a grass base, fine detail grain, and dirt patches together.

typedef struct {
  Texture2D grassBase;   // real grass photo texture
  Texture2D grassDetail; // fine grayscale grain/speckle overlay, ~0.5-centered
                         // for symmetric multiply
  Texture2D dirt;        // dirt patch photo texture

  float baseTileSize;    // world meters per grassBase/dirt tile
  float detailTileSize;  // world meters per grassDetail tile — much smaller
                         // than baseTileSize (denser repeat) for fine grain
  float maskNoiseScale;  // world-space fbm2 frequency controlling where dirt
                         // shows through grass (procedural — no mask texture)
  float colorVarScale;   // world-space fbm2 frequency for broad brightness/
                         // tint variation across the whole ground (procedural
                         // — no noise texture)
} GrassMaterialConfig;

// Lazy-loads (and caches, via ResourceManager_LoadShader) the shared
// grass_material.vs/.fs shader. Safe to call every frame — cheap cache
// lookup after the first call.
Shader GrassMaterial_GetShader(void);

// Builds a raylib Material bound to the shared grass_material shader.
// Binds grassBase/grassDetail/dirt into the same 3 texture-map slots
// prop_lit.c uses (MATERIAL_MAP_DIFFUSE/NORMAL/ROUGHNESS as generic
// texture-slot carriers, not for their raylib-semantic meaning), and sets
// baseTileSize/detailTileSize/maskNoiseScale/colorVarScale as shader
// uniforms ONCE at creation time (they are static per-material, unlike
// lighting which is pushed every frame). Call once per ground mesh (e.g.
// in a map's Init), not per draw call/instance.
Material GrassMaterial_Make(GrassMaterialConfig config);

// Pushes the current Environment_Get{SunDirection,SunColor,AmbientColor}()
// values into the shared grass_material shader as uniforms.
// DrawModel()/DrawModelEx() do NOT auto-bind these — that's a
// SkillManager_BeginShader-only convention that never runs for a plain
// DrawModel() call — so call this once per frame before drawing any
// grass_material ground, so lighting responds to
// Environment_SetSunColor()/SetAmbientColor() changes (e.g. a day/night
// cycle). Same contract as PropLit_UpdateLighting.
void GrassMaterial_UpdateLighting(void);

#endif // MAP_TOOLKIT_GRASS_MATERIAL_H
