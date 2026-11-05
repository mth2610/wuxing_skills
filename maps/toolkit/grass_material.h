#ifndef MAP_TOOLKIT_GRASS_MATERIAL_H
#define MAP_TOOLKIT_GRASS_MATERIAL_H

#include "raylib.h"

// grass_material: texture-blend hybrid ground material (CORE_ISSUES.md
// Item 38, supersedes the pure-procedural Item 37 version).

typedef struct
{
  Texture2D grassBase;   // real grass photo texture
  Texture2D grassDetail; // fine grayscale grain/speckle overlay, ~0.5-centered
                         // for symmetric multiply
  Texture2D dirt;        // dirt patch photo texture

  float baseTileSize;   // world meters per grassBase/dirt tile
  float detailTileSize; // world meters per grassDetail tile — much smaller
                        // than baseTileSize (denser repeat) for fine grain
  float maskNoiseScale; // world-space fbm2 frequency controlling where dirt
                        // shows through grass (procedural — no mask texture)
  float colorVarScale;  // world-space fbm2 frequency for broad brightness/
                        // tint variation across the whole ground (procedural
                        // — no noise texture)
} GrassMaterialConfig;

// Lazy-loads (and caches, via ResourceManager_LoadShader) the shared
// grass_material.vs/.fs shader. Safe to call every frame — cheap cache
// lookup after the first call.
Shader GrassMaterial_GetShader(void);

// Builds a raylib Material bound to the shared grass_material shader.
Material GrassMaterial_Make(GrassMaterialConfig config);

// Pushes the current Environment_Get{SunDirection,SunColor,AmbientColor}()
// values into the shared grass_material shader as uniforms.
void GrassMaterial_UpdateLighting(void);

#endif // MAP_TOOLKIT_GRASS_MATERIAL_H