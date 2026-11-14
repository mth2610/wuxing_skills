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
    if (mesh == NULL || mesh->texcoords == NULL || tileSize <= 1e-6f)
        return;

    const float invTile = 1.0f / tileSize;
    const float repeatU = worldWidth * invTile;
    const float repeatV = worldLength * invTile;
    float *restrict uv = mesh->texcoords;
    const int count = mesh->vertexCount;
    for (int i = 0; i < count; i++, uv += 2)
    {
        uv[0] *= repeatU;
        uv[1] *= repeatV;
    }
}

static inline unsigned int Nature_NextRandom(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

// Trả về giá trị trong nửa khoảng [0.0f, 1.0f) bằng nhân lũy thừa 2 (0x1.0p-24f),
// ngăn lỗi tràn chỉ số mảng (idx == count) khi dùng trực tiếp làm hệ số chọn phần tử.
static inline float Nature_Random01(unsigned int *state)
{
    return (float)(Nature_NextRandom(state) & 0x00ffffffu) * (1.0f / 16777216.0f);
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
    return (float)(Nature_HashU32(x) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

// Tính đồng thời sin và cos với phép rút gọn miền [-pi/2, pi/2] không gọi libm floorf()
// và đa thức minimax bậc cao giúp tăng tốc khởi tạo/cập nhật hàng trăm nghìn prop.
static inline void Nature_FastSinCos(float angleRad, float *restrict outSin, float *restrict outCos)
{
    const float kInvPi = 0.31830988618379067f;
    const float kPiHi = 3.14159250259399414f;
    const float kPiLo = 1.5099579909783764e-7f;

    float y = angleRad * kInvPi;
    int q = (int)(y >= 0.0f ? (y + 0.5f) : (y - 0.5f));
    float qf = (float)q;
    float x = (angleRad - qf * kPiHi) - qf * kPiLo;
    float x2 = x * x;

    float s = ((-0.00019840874f * x2 + 0.0083333310f) * x2 - 0.16666667f) * (x * x2) + x;
    float c = ((-0.0013888378f * x2 + 0.041666638f) * x2 - 0.5f) * x2 + 1.0f;

    if (q & 1)
    {
        s = -s;
        c = -c;
    }
    *outSin = s;
    *outCos = c;
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
#include "maps/toolkit/map_props_rain.inl"
#include "maps/toolkit/map_props_nature.inl"
#include "maps/toolkit/map_props_flora.inl"
#include "maps/toolkit/map_props_debris.inl"
#include "maps/toolkit/map_props_fluff.inl"
#include "maps/toolkit/map_props_dem.inl"