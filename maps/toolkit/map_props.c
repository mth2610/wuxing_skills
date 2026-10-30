#include "maps/toolkit/map_props.h"
#include "maps/map_manager.h"
#include "core/resource_manager.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_shadow.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "core/vfx_light.h"             // Đợt E / E2 — VFXLight_BindToShader on the ground
#include "post_process/scene_targets.h" // Depth buffer for soft particles & shallow water
#include "core/gfx_quality.h"
#include "post_process/camera_context.h"
#include "environment/wind/wind_system.h"
#include "core/motion/motion_fields.h"
#include "core/time_fx.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h> // getenv (WUXING_GROUND_LOOKUP_VERIFY)

// Split into one .inl per prop kind (ground/strip/rocks) — each grew its own
// shader + statics and stopped reading well as one flat file. #include'd
// below rather than compiled separately, same convention as skills'
// _params.inl/_tunables.inl split — one translation unit, easier-to-scan
// pieces. Add a new prop kind by adding a new map_props_<kind>.inl + #include
// here + declaring it in map_props.h.

static inline void TilePlaneUVs(Mesh *mesh, float worldWidth, float worldLength, float tileSize)
{
    float repeatU = worldWidth / tileSize;
    float repeatV = worldLength / tileSize;
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        mesh->texcoords[i * 2 + 0] *= repeatU;
        mesh->texcoords[i * 2 + 1] *= repeatV;
    }
}

static inline unsigned int Nature_NextRandom(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static inline float Nature_Random01(unsigned int *state)
{
    return (float)(Nature_NextRandom(state) & 0x00ffffffu) / 16777215.0f;
}

// Băm số nguyên cực nhanh phục vụ chọn loài hoa, góc xoay và tỉa mật độ LOD ở cự ly xa
static inline unsigned int Nature_HashU32(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline float Nature_Hash01(unsigned int x)
{
    return (float)(Nature_HashU32(x) & 0x00ffffffu) * (1.0f / 16777215.0f);
}

static inline void Nature_FastSinCos(float angleRad, float *outSin, float *outCos)
{
    *outSin = sinf(angleRad);
    *outCos = cosf(angleRad);
}

#include "maps/toolkit/map_props_texture.inl"
#include "maps/toolkit/map_props_boundary.inl"
#include "maps/toolkit/map_props_boundary_contour.inl"
#include "maps/toolkit/map_props_ground.inl"
#include "maps/toolkit/map_props_strip.inl"
#include "maps/toolkit/map_props_cull.inl"
#include "maps/toolkit/map_props_rocks.inl"
#include "maps/toolkit/map_props_cloud.inl"
#include "maps/toolkit/map_props_cliff.inl"
#include "maps/toolkit/map_props_sky.inl"
#include "maps/toolkit/map_props_mesh_index.inl"
#include "maps/toolkit/map_props_nature.inl"
#include "maps/toolkit/map_props_flora.inl"
#include "maps/toolkit/map_props_debris.inl"
#include "maps/toolkit/map_props_fluff.inl"
#include "maps/toolkit/map_props_dem.inl"