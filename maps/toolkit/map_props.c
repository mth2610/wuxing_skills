#include "maps/toolkit/map_props.h"
#include "core/resource_manager.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_shadow.h"
#include "environment/environment_system.h"
#include "core/vfx_light.h"   // Đợt E / E2 — VFXLight_BindToShader on the ground
#include "core/gfx_quality.h"
#include "core/camera_context.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>   // getenv (WUXING_GROUND_LOOKUP_VERIFY)

// Split into one .inl per prop kind (ground/strip/rocks) — each grew its own
// shader + statics and stopped reading well as one flat file. #include'd
// below rather than compiled separately, same convention as skills'
// _params.inl/_tunables.inl split — one translation unit, easier-to-scan
// pieces. Add a new prop kind by adding a new map_props_<kind>.inl + #include
// here + declaring it in map_props.h.

// raylib's GenMeshPlane UVs span a flat 0..1 across the whole plane; scale
// them so the texture repeats every `tileSize` meters instead of stretching
// once across the mesh. Shared helper, currently unused by ground/strip
// (both keep raw 0..1 UV for their own reasons — see their .inl files) but
// kept for future prop kinds (bush/flower patch) that want simple tiling.
static void TilePlaneUVs(Mesh *mesh, float worldWidth, float worldLength, float tileSize)
{
    float repeatU = worldWidth / tileSize;
    float repeatV = worldLength / tileSize;
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        mesh->texcoords[i * 2 + 0] *= repeatU;
        mesh->texcoords[i * 2 + 1] *= repeatV;
    }
}

#include "maps/toolkit/map_props_ground.inl"
#include "maps/toolkit/map_props_strip.inl"
#include "maps/toolkit/map_props_rocks.inl"
#include "maps/toolkit/map_props_cloud.inl"
#include "maps/toolkit/map_props_nature.inl"
