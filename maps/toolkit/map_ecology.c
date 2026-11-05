#include "maps/toolkit/map_ecology.h"
#include "maps/toolkit/meadow_palette.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint8_t s_habitat[MAP_ECOLOGY_RES * MAP_ECOLOGY_RES * 4];
static uint8_t s_distance[MAP_ECOLOGY_RES * MAP_ECOLOGY_RES * 4];
static const MapEcology *s_owner;

static inline float EcoClamp(float v, float a, float b)
{
    return (v < a) ? a : ((v > b) ? b : v);
}

static inline float EcoSmooth(float a, float b, float v)
{
    v = EcoClamp((v - a) / (b - a), 0.0f, 1.0f);
    return v * v * (3.0f - 2.0f * v);
}

float MapEcology_RoadDistance(const MapEcologyConfig *c, float x, float z)
{
    if (!c || c->pathCount <= 0 || !c->paths)
        return MAP_ECOLOGY_DISTANCE_RANGE;

    int count = c->pathCount < MAP_ECOLOGY_MAX_PATHS ? c->pathCount : MAP_ECOLOGY_MAX_PATHS;
    float maxRange = MAP_ECOLOGY_DISTANCE_RANGE + c->roadHalfWidth;
    float minDistSq = maxRange * maxRange;

    for (int i = 0; i < count; i++)
    {
        Vector4 p = c->paths[i];
        float dx = p.z - p.x;
        float dz = p.w - p.y;
        float lenSq = dx * dx + dz * dz;
        float t = (lenSq > 1e-8f) ? EcoClamp(((x - p.x) * dx + (z - p.y) * dz) / lenSq, 0.0f, 1.0f) : 0.0f;
        float qx = x - p.x - t * dx;
        float qz = z - p.y - t * dz;
        float dSq = qx * qx + qz * qz;
        if (dSq < minDistSq)
            minDistSq = dSq;
    }

    float d = sqrtf(minDistSq) - c->roadHalfWidth;
    return (d < MAP_ECOLOGY_DISTANCE_RANGE) ? d : MAP_ECOLOGY_DISTANCE_RANGE;
}

float MapEcology_ShoreDistance(Vector4 lake, float x, float z)
{
    if (lake.z <= 0.0f || lake.w <= 0.0f)
        return MAP_ECOLOGY_DISTANCE_RANGE;

    float a = fmaxf(lake.z, lake.w);
    float b = fminf(lake.z, lake.w);
    float px = fabsf(x - lake.x);
    float py = fabsf(z - lake.y);
    if (lake.z < lake.w)
    {
        float q = px;
        px = py;
        py = q;
    }

    // Fast exact path for circular lakes
    if (a - b < 1e-5f)
        return sqrtf(px * px + py * py) - a;

    float a2 = a * a;
    float b2 = b * b;
    float sign = (px * px / a2 + py * py / b2 < 1.0f) ? -1.0f : 1.0f;
    if (py < 1e-6f)
    {
        float cx = a * px / (a2 - b2 + 1e-8f);
        if (cx < 1.0f)
        {
            float qx = a * cx;
            float qy = b * sqrtf(fmaxf(0.0f, 1.0f - cx * cx));
            return sign * sqrtf((qx - px) * (qx - px) + qy * qy);
        }
        return sign * fabsf(a - px);
    }

    float lo = -b2 + b * py * 0.00001f;
    float hi = fmaxf(a * px + b * py, a2);
    float apx = a * px;
    float bpy = b * py;
    for (int i = 0; i < 16; i++)
    {
        float t = (lo + hi) * 0.5f;
        float u = apx / (t + a2);
        float v = bpy / (t + b2);
        if (u * u + v * v > 1.0f)
            lo = t;
        else
            hi = t;
    }
    float t = (lo + hi) * 0.5f;
    // Two Newton-Raphson refinement steps for full float32 accuracy
    for (int i = 0; i < 2; i++)
    {
        float invA = 1.0f / (t + a2);
        float invB = 1.0f / (t + b2);
        float u = apx * invA;
        float v = bpy * invB;
        float f = u * u + v * v - 1.0f;
        float df = -2.0f * (u * u * invA + v * v * invB);
        if (fabsf(df) > 1e-12f)
        {
            float nextT = t - f / df;
            if (nextT >= lo && nextT <= hi)
                t = nextT;
        }
    }
    float qx = a2 * px / (t + a2);
    float qy = b2 * py / (t + b2);
    return sign * sqrtf((qx - px) * (qx - px) + (qy - py) * (qy - py));
}

static inline void EcoEncode(float v, uint8_t *p)
{
    float u = EcoClamp((v * (1.0f / MAP_ECOLOGY_DISTANCE_RANGE) + 1.0f) * 0.5f, 0.0f, 1.0f) * 255.0f;
    float fl = floorf(u);
    p[0] = (uint8_t)fl;
    p[1] = (uint8_t)((u - fl) * 255.0f + 0.5f);
}

static inline float EcoDecode(const uint8_t *p)
{
    return (((float)p[0] + (float)p[1] * (1.0f / 255.0f)) * (2.0f / 255.0f) - 1.0f) * MAP_ECOLOGY_DISTANCE_RANGE;
}

typedef struct
{
    float x0, z0, dx, dz, invLenSq;
} EcoPrecompPath;

bool MapEcology_Bake(MapEcology *m, const MapEcologyConfig *c,
                     MapEcologyDensityFn density, MapEcologyEligibleFn eligible, void *user)
{
    if (!m || !c || c->rect.z <= 0.0f || c->rect.w <= 0.0f || c->pathCount < 0 ||
        (c->pathCount > 0 && !c->paths) || (s_owner && s_owner != m))
        return false;
    if (m->ready)
        return true;

    double start = GetTime();

    // 1. Precompute road segment geometry and bounding box once
    int pathCount = c->pathCount < MAP_ECOLOGY_MAX_PATHS ? c->pathCount : MAP_ECOLOGY_MAX_PATHS;
    EcoPrecompPath prePaths[MAP_ECOLOGY_MAX_PATHS];
    float roadMinX = 1e30f, roadMaxX = -1e30f, roadMinZ = 1e30f, roadMaxZ = -1e30f;
    for (int i = 0; i < pathCount; i++)
    {
        Vector4 p = c->paths[i];
        float dx = p.z - p.x;
        float dz = p.w - p.y;
        float lenSq = dx * dx + dz * dz;
        prePaths[i] = (EcoPrecompPath){
            .x0 = p.x,
            .z0 = p.y,
            .dx = dx,
            .dz = dz,
            .invLenSq = (lenSq > 1e-8f) ? (1.0f / lenSq) : 0.0f};
        float loX = fminf(p.x, p.z), hiX = fmaxf(p.x, p.z);
        float loZ = fminf(p.y, p.w), hiZ = fmaxf(p.y, p.w);
        if (loX < roadMinX)
            roadMinX = loX;
        if (hiX > roadMaxX)
            roadMaxX = hiX;
        if (loZ < roadMinZ)
            roadMinZ = loZ;
        if (hiZ > roadMaxZ)
            roadMaxZ = hiZ;
    }
    float maxRoadReach = MAP_ECOLOGY_DISTANCE_RANGE + c->roadHalfWidth;
    roadMinX -= maxRoadReach;
    roadMaxX += maxRoadReach;
    roadMinZ -= maxRoadReach;
    roadMaxZ += maxRoadReach;
    float maxRoadReachSq = maxRoadReach * maxRoadReach;

    // 2. Precompute 1D separable trigonometric tables for MeadowHabitat(wx, wz)
    // Eliminates 2,097,152 sinf() calls across the 1024x1024 grid
    float wxTable[MAP_ECOLOGY_RES];
    float wzTable[MAP_ECOLOGY_RES];
    float sinX0[MAP_ECOLOGY_RES], cosX0[MAP_ECOLOGY_RES];
    float sinZ0[MAP_ECOLOGY_RES], cosZ0[MAP_ECOLOGY_RES];
    float sinX1[MAP_ECOLOGY_RES], cosX1[MAP_ECOLOGY_RES];
    float sinZ1[MAP_ECOLOGY_RES], cosZ1[MAP_ECOLOGY_RES];

    const float stepX = c->rect.z * (1.0f / (float)MAP_ECOLOGY_RES);
    const float stepZ = c->rect.w * (1.0f / (float)MAP_ECOLOGY_RES);
    for (int i = 0; i < MAP_ECOLOGY_RES; i++)
    {
        float wx = c->rect.x + ((float)i + 0.5f) * stepX;
        float wz = c->rect.y + ((float)i + 0.5f) * stepZ;
        wxTable[i] = wx;
        wzTable[i] = wz;

        float ax0 = wx * 0.15f;
        float az0 = wz * 0.10f;
        sinX0[i] = sinf(ax0);
        cosX0[i] = cosf(ax0);
        sinZ0[i] = sinf(az0);
        cosZ0[i] = cosf(az0);

        float ax1 = wx * -0.11f;
        float az1 = wz * 0.20f + 1.8f;
        sinX1[i] = sinf(ax1);
        cosX1[i] = cosf(ax1);
        sinZ1[i] = sinf(az1);
        cosZ1[i] = cosf(az1);
    }

    bool hasLake = (c->lake.z > 0.0f && c->lake.w > 0.0f);
    float lakeMaxR = fmaxf(c->lake.z, c->lake.w) + MAP_ECOLOGY_DISTANCE_RANGE;
    float lakeMaxRSq = lakeMaxR * lakeMaxR;

    for (int y = 0; y < MAP_ECOLOGY_RES; y++)
    {
        float wz = wzTable[y];
        float sz0 = sinZ0[y], cz0 = cosZ0[y];
        float sz1 = sinZ1[y], cz1 = cosZ1[y];
        bool zInRoadBox = (pathCount > 0 && wz >= roadMinZ && wz <= roadMaxZ);
        float dzLake = wz - c->lake.y;
        float dzLakeSq = dzLake * dzLake;
        int rowBase = y * MAP_ECOLOGY_RES * 4;

        for (int x = 0; x < MAP_ECOLOGY_RES; x++)
        {
            float wx = wxTable[x];
            int k = rowBase + x * 4;

            float road = MAP_ECOLOGY_DISTANCE_RANGE;
            if (zInRoadBox && wx >= roadMinX && wx <= roadMaxX)
            {
                float minDistSq = maxRoadReachSq;
                for (int i = 0; i < pathCount; i++)
                {
                    const EcoPrecompPath *sp = &prePaths[i];
                    float rx = wx - sp->x0;
                    float rz = wz - sp->z0;
                    float t = EcoClamp((rx * sp->dx + rz * sp->dz) * sp->invLenSq, 0.0f, 1.0f);
                    float qx = rx - t * sp->dx;
                    float qz = rz - t * sp->dz;
                    float dSq = qx * qx + qz * qz;
                    if (dSq < minDistSq)
                        minDistSq = dSq;
                }
                if (minDistSq < maxRoadReachSq)
                    road = sqrtf(minDistSq) - c->roadHalfWidth;
            }

            float shore = MAP_ECOLOGY_DISTANCE_RANGE;
            if (hasLake)
            {
                float dxLake = wx - c->lake.x;
                if (dxLake * dxLake + dzLakeSq < lakeMaxRSq)
                    shore = MapEcology_ShoreDistance(c->lake, wx, wz);
            }

            float cover = density ? density(wx, wz, user) : 1.0f;
            if (eligible && !eligible(wx, wz, user))
                cover = 0.0f;

            // Exact identity: sin(ax + az) = sin(ax)*cos(az) + cos(ax)*sin(az)
            float s0 = sinX0[x] * cz0 + cosX0[x] * sz0;
            float s1 = sinX1[x] * cz1 + cosX1[x] * sz1;
            float habitat = (s0 * 0.5f + 0.5f) * 0.6f + (s1 * 0.5f + 0.5f) * 0.4f;

            float moisture = (shore <= 0.0f) ? 1.0f : ((shore >= 3.0f) ? 0.0f : (1.0f - EcoSmooth(0.0f, 3.0f, shore)));
            float canopy = habitat > 0.60f   ? 0.36f + (habitat - 0.60f) * (0.06f / 0.40f)
                           : habitat < 0.35f ? 0.30f + habitat * (0.04f / 0.35f)
                                             : 0.33f + (habitat - 0.35f) * (0.04f / 0.25f);
            float growth = EcoClamp((canopy - 0.30f) * (1.0f / 0.12f) + moisture * 0.025f, 0.0f, 1.0f);

            s_habitat[k + 0] = (uint8_t)(EcoClamp(cover, 0.0f, 1.0f) * 255.0f + 0.5f);
            s_habitat[k + 1] = (uint8_t)(growth * 255.0f + 0.5f);
            s_habitat[k + 2] = (uint8_t)(moisture * 255.0f + 0.5f);
            s_habitat[k + 3] = (uint8_t)(EcoClamp(habitat, 0.0f, 1.0f) * 255.0f + 0.5f);
            EcoEncode(road, s_distance + k);
            EcoEncode(shore, s_distance + k + 2);
        }
    }

    Image h = {s_habitat, MAP_ECOLOGY_RES, MAP_ECOLOGY_RES, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Image d = {s_distance, MAP_ECOLOGY_RES, MAP_ECOLOGY_RES, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    m->habitatTexture = LoadTextureFromImage(h);
    m->distanceTexture = LoadTextureFromImage(d);
    if (!m->habitatTexture.id || !m->distanceTexture.id)
    {
        MapEcology_Unload(m);
        return false;
    }
    SetTextureFilter(m->habitatTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(m->distanceTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(m->habitatTexture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(m->distanceTexture, TEXTURE_WRAP_CLAMP);
    m->rect = c->rect;
    m->ready = true;
    s_owner = m;
    TraceLog(LOG_INFO, "MAP_ECOLOGY: 1024x1024 8MiB bake %.2f ms", (GetTime() - start) * 1000.0);
    return true;
}

MapEcologySample MapEcology_Sample(const MapEcology *m, float x, float z)
{
    MapEcologySample out = {0};
    if (!m || !m->ready || m != s_owner ||
        x < m->rect.x || z < m->rect.y ||
        x > m->rect.x + m->rect.z || z > m->rect.y + m->rect.w)
        return out;

    float fx = EcoClamp((x - m->rect.x) / m->rect.z * (float)MAP_ECOLOGY_RES - 0.5f, 0.0f, (float)(MAP_ECOLOGY_RES - 1));
    float fy = EcoClamp((z - m->rect.y) / m->rect.w * (float)MAP_ECOLOGY_RES - 0.5f, 0.0f, (float)(MAP_ECOLOGY_RES - 1));
    int ix = (int)fx;
    int iy = (int)fy;
    int jx = (ix + 1 < MAP_ECOLOGY_RES) ? (ix + 1) : ix;
    int jy = (iy + 1 < MAP_ECOLOGY_RES) ? (iy + 1) : iy;
    float tx = fx - (float)ix;
    float ty = fy - (float)iy;
    float omx = 1.0f - tx;
    float omy = 1.0f - ty;

    float w00 = omx * omy;
    float w10 = tx * omy;
    float w01 = omx * ty;
    float w11 = tx * ty;

    int k00 = (iy * MAP_ECOLOGY_RES + ix) * 4;
    int k10 = (iy * MAP_ECOLOGY_RES + jx) * 4;
    int k01 = (jy * MAP_ECOLOGY_RES + ix) * 4;
    int k11 = (jy * MAP_ECOLOGY_RES + jx) * 4;

    const float inv255 = 1.0f / 255.0f;
    out.coverage = (s_habitat[k00 + 0] * w00 + s_habitat[k10 + 0] * w10 + s_habitat[k01 + 0] * w01 + s_habitat[k11 + 0] * w11) * inv255;
    out.growth = (s_habitat[k00 + 1] * w00 + s_habitat[k10 + 1] * w10 + s_habitat[k01 + 1] * w01 + s_habitat[k11 + 1] * w11) * inv255;
    out.moisture = (s_habitat[k00 + 2] * w00 + s_habitat[k10 + 2] * w10 + s_habitat[k01 + 2] * w01 + s_habitat[k11 + 2] * w11) * inv255;
    out.habitat = (s_habitat[k00 + 3] * w00 + s_habitat[k10 + 3] * w10 + s_habitat[k01 + 3] * w01 + s_habitat[k11 + 3] * w11) * inv255;
    out.roadDistance = EcoDecode(s_distance + k00) * w00 + EcoDecode(s_distance + k10) * w10 +
                       EcoDecode(s_distance + k01) * w01 + EcoDecode(s_distance + k11) * w11;
    out.shoreDistance = EcoDecode(s_distance + k00 + 2) * w00 + EcoDecode(s_distance + k10 + 2) * w10 +
                        EcoDecode(s_distance + k01 + 2) * w01 + EcoDecode(s_distance + k11 + 2) * w11;
    return out;
}

void MapEcology_Unload(MapEcology *m)
{
    if (!m)
        return;
    if (m->habitatTexture.id)
        UnloadTexture(m->habitatTexture);
    if (m->distanceTexture.id)
        UnloadTexture(m->distanceTexture);
    if (s_owner == m)
        s_owner = NULL;
    memset(m, 0, sizeof(*m));
}