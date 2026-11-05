// DEM-based Island System (sampling, heightfield water, abyss detection, and MapManager query hooks)
#ifndef MAP_PROPS_DEM_INL
#define MAP_PROPS_DEM_INL

#include "maps/toolkit/map_props.h"
#include "environment/env_shadow.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

unsigned int MapProp_GetSkipMask(void)
{
    unsigned int mask = 0;
    const char *env = getenv("WUXING_MAP_SKIP");
    if (!env || !*env)
        env = getenv("WUXING_SONGQUAO_SKIP");
    if (!env || !*env)
        return 0;

    if (strstr(env, "ground"))
        mask |= MAP_SKIP_GROUND;
    if (strstr(env, "rocks"))
        mask |= MAP_SKIP_ROCKS;
    if (strstr(env, "debris"))
        mask |= MAP_SKIP_DEBRIS;
    if (strstr(env, "flora") || strstr(env, "grass"))
        mask |= MAP_SKIP_FLORA;
    if (strstr(env, "sky"))
        mask |= MAP_SKIP_SKY;
    if (strstr(env, "cloudsea"))
        mask |= MAP_SKIP_CLOUDSEA;
    if (strstr(env, "water"))
        mask |= MAP_SKIP_WATER;
    if (strstr(env, "mist"))
        mask |= MAP_SKIP_MIST;
    if (strstr(env, "shadowcasters") || strstr(env, "casters"))
        mask |= MAP_SKIP_SHADOWCASTERS;
    if (strstr(env, "fluff"))
        mask |= MAP_SKIP_FLUFF;
    if (strstr(env, "cloudshadow"))
        mask |= MAP_SKIP_CLOUDSHADOW;
    if (strstr(env, "fogvolumes"))
        mask |= MAP_SKIP_FOGVOLUMES;
    if (strstr(env, "godrays"))
        mask |= MAP_SKIP_GODRAYS;
    if (strstr(env, "grade"))
        mask |= MAP_SKIP_GRADE;
    if (strstr(env, "cliff"))
        mask |= MAP_SKIP_CLIFF;

    return mask;
}

bool MapDem_InitIsland(MapDemIsland *dem,
                       float mapWidth, float mapDepth,
                       float heightRange, float cliffDepth,
                       const char *splatPath,
                       const char *waterHeightPath)
{
    if (!dem || mapWidth <= 0.0f || mapDepth <= 0.0f)
        return false;
    memset(dem, 0, sizeof(*dem));
    dem->mapWidth = mapWidth;
    dem->mapDepth = mapDepth;
    dem->heightRange = heightRange;
    dem->cliffDepth = cliffDepth;
    dem->mapCenter = (Vector3){mapWidth * 0.5f, 0.0f, mapDepth * 0.5f};

    if (splatPath && splatPath[0])
    {
        dem->splatImage = LoadImage(splatPath);
        if (dem->splatImage.data != NULL &&
            dem->splatImage.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        {
            ImageFormat(&dem->splatImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        }
        dem->splatLoaded = (dem->splatImage.data != NULL);
    }
    if (waterHeightPath && waterHeightPath[0])
    {
        dem->waterHeightImage = LoadImage(waterHeightPath);
        if (dem->waterHeightImage.data != NULL &&
            dem->waterHeightImage.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        {
            ImageFormat(&dem->waterHeightImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        }
        dem->waterHeightLoaded = (dem->waterHeightImage.data != NULL);
    }

    dem->ready = true;
    return true;
}

void MapDem_UnloadIsland(MapDemIsland *dem)
{
    if (!dem)
        return;
    if (dem->splatLoaded)
    {
        UnloadImage(dem->splatImage);
        dem->splatImage = (Image){0};
        dem->splatLoaded = false;
    }
    if (dem->waterHeightLoaded)
    {
        UnloadImage(dem->waterHeightImage);
        dem->waterHeightImage = (Image){0};
        dem->waterHeightLoaded = false;
    }
    dem->ready = false;
}

static inline Color MapDem_ReadPixelFast(const Image *img, int x, int y)
{
    if (img->format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        return ((const Color *)img->data)[y * img->width + x];
    }
    return GetImageColor(*img, x, y);
}

Color MapDem_SampleSplat(const MapDemIsland *dem, float x, float z)
{
    if (!dem || !dem->splatLoaded || !dem->splatImage.data ||
        dem->splatImage.width <= 0 || dem->splatImage.height <= 0)
        return (Color){255, 0, 0, 0};

    int w = dem->splatImage.width;
    int h = dem->splatImage.height;
    float u = (x / dem->mapWidth) * (float)(w - 1);
    float v = (z / dem->mapDepth) * (float)(h - 1);
    int tx = (u <= 0.0f) ? 0 : ((u >= (float)(w - 1)) ? (w - 1) : (int)(u + 0.5f));
    int tz = (v <= 0.0f) ? 0 : ((v >= (float)(h - 1)) ? (h - 1) : (int)(v + 0.5f));

    return MapDem_ReadPixelFast(&dem->splatImage, tx, tz);
}

float MapDem_SampleWaterHeight(const MapDemIsland *dem, float x, float z)
{
    if (!dem || !dem->waterHeightLoaded || !dem->waterHeightImage.data ||
        dem->waterHeightImage.width < 2 || dem->waterHeightImage.height < 2 ||
        x < 0.0f || z < 0.0f || x > dem->mapWidth || z > dem->mapDepth)
    {
        return dem ? -dem->cliffDepth : -8.0f;
    }

    int w = dem->waterHeightImage.width;
    int h = dem->waterHeightImage.height;
    float fx = (x / dem->mapWidth) * (float)(w - 1);
    float fz = (z / dem->mapDepth) * (float)(h - 1);
    int ix = (int)fx;
    int iz = (int)fz;
    if (ix < 0)
        ix = 0;
    else if (ix > w - 2)
        ix = w - 2;
    if (iz < 0)
        iz = 0;
    else if (iz > h - 2)
        iz = h - 2;
    float tx = fx - (float)ix;
    float tz = fz - (float)iz;

    Color c00, c10, c01, c11;
    if (dem->waterHeightImage.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        const Color *row0 = &((const Color *)dem->waterHeightImage.data)[iz * w + ix];
        const Color *row1 = row0 + w;
        c00 = row0[0];
        c10 = row0[1];
        c01 = row1[0];
        c11 = row1[1];
    }
    else
    {
        c00 = GetImageColor(dem->waterHeightImage, ix, iz);
        c10 = GetImageColor(dem->waterHeightImage, ix + 1, iz);
        c01 = GetImageColor(dem->waterHeightImage, ix, iz + 1);
        c11 = GetImageColor(dem->waterHeightImage, ix + 1, iz + 1);
    }

    // Quick dry-cell rejection when all 4 surrounding texels have zero wet mask (B == 0)
    if ((c00.b | c10.b | c01.b | c11.b) == 0)
        return -dem->cliffDepth;

    float h00 = (float)(((unsigned int)c00.r << 8) | (unsigned int)c00.g);
    float h10 = (float)(((unsigned int)c10.r << 8) | (unsigned int)c10.g);
    float h01 = (float)(((unsigned int)c01.r << 8) | (unsigned int)c01.g);
    float h11 = (float)(((unsigned int)c11.r << 8) | (unsigned int)c11.g);
    float code = (tx + tz <= 1.0f)
                     ? (h00 + tx * (h10 - h00) + tz * (h01 - h00))
                     : (h11 + (1.0f - tx) * (h01 - h11) + (1.0f - tz) * (h10 - h11));
    return code * ((dem->cliffDepth + dem->heightRange) * (1.0f / 65535.0f)) - dem->cliffDepth;
}

static inline bool MapDem_IsSplatAbyssCandidate(const MapDemIsland *dem, float x, float z)
{
    Color splat = MapDem_SampleSplat(dem, x, z);
    return (splat.g == 255 && splat.r == 0 && splat.b == 0 && splat.a == 0);
}

bool MapDem_IsAbyss(const MapDemIsland *dem, const MapGroundSurface *ground, float x, float z)
{
    if (!dem || !dem->ready || x < 1.0f || x > dem->mapWidth - 1.0f || z < 1.0f || z > dem->mapDepth - 1.0f)
        return true;

    if (MapDem_IsSplatAbyssCandidate(dem, x, z))
    {
        float rawY = MapProp_SampleGroundHeight(ground, dem->mapCenter, x, z);
        if (rawY <= -dem->cliffDepth + 0.15f)
            return true;
    }
    return false;
}

float MapDem_GetGroundHeight(const MapDemIsland *dem, const MapGroundSurface *ground, float x, float z)
{
    if (!dem || !dem->ready || !ground ||
        x < 1.0f || x > dem->mapWidth - 1.0f || z < 1.0f || z > dem->mapDepth - 1.0f)
        return -100.0f;

    float rawY = MapProp_SampleGroundHeight(ground, dem->mapCenter, x, z);
    if (rawY <= -dem->cliffDepth + 0.15f && MapDem_IsSplatAbyssCandidate(dem, x, z))
        return -100.0f;
    return rawY;
}

bool MapDem_SampleGroundSurface(const MapDemIsland *dem, const MapGroundSurface *ground,
                                float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (!dem || !dem->ready || !ground ||
        x < 1.0f || x > dem->mapWidth - 1.0f || z < 1.0f || z > dem->mapDepth - 1.0f)
    {
        if (outPosition)
            *outPosition = (Vector3){x, -100.0f, z};
        if (outNormal)
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        return false;
    }

    Vector3 pos;
    Vector3 norm;
    if (!MapProp_SampleGroundSurface(ground, dem->mapCenter, x, z, &pos, outNormal ? &norm : NULL))
    {
        if (outPosition)
            *outPosition = (Vector3){x, -100.0f, z};
        if (outNormal)
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        return false;
    }

    if (pos.y <= -dem->cliffDepth + 0.15f && MapDem_IsSplatAbyssCandidate(dem, x, z))
    {
        if (outPosition)
            *outPosition = (Vector3){x, -100.0f, z};
        if (outNormal)
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        return false;
    }

    if (outPosition)
        *outPosition = pos;
    if (outNormal)
        *outNormal = norm;
    return true;
}

bool MapDem_GetWaterInfo(const MapDemIsland *dem, const MapGroundSurface *ground,
                         float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!dem || !dem->ready || !ground || x < 0.0f || z < 0.0f || x > dem->mapWidth || z > dem->mapDepth)
        return false;

    float surfaceY = MapDem_SampleWaterHeight(dem, x, z);
    if (surfaceY <= -dem->cliffDepth + 0.05f)
        return false;

    float gy = MapProp_SampleGroundHeight(ground, dem->mapCenter, x, z);
    if (surfaceY <= gy + 0.005f)
        return false;

    if (outSurfaceY)
        *outSurfaceY = surfaceY;
    if (outWaterDepth)
        *outWaterDepth = surfaceY - gy;
    return true;
}

void MapDem_SetWaterInteractor(const MapDemIsland *dem, const MapGroundSurface *ground,
                               MapWaterSurface *water, Vector3 position, Vector3 velocity, float radius)
{
    if (!water)
        return;
    if (!dem || !dem->ready || !ground || position.x < 0.0f || position.z < 0.0f ||
        position.x > dem->mapWidth || position.z > dem->mapDepth)
    {
        MapProp_SetWaterInteractor(water, position, velocity, radius, 0.0f);
        return;
    }

    float surfaceY = MapDem_SampleWaterHeight(dem, position.x, position.z);
    if (surfaceY > -dem->cliffDepth + 0.05f)
    {
        float gy = MapProp_SampleGroundHeight(ground, dem->mapCenter, position.x, position.z);
        if (surfaceY > gy + 0.005f)
        {
            float depth = surfaceY - gy;
            float submerged = 0.0f;
            if (position.y < surfaceY + 0.20f)
            {
                submerged = fminf(1.0f, fmaxf(0.0f, (surfaceY - position.y + 0.25f) / fmaxf(depth, 0.35f)));
            }
            MapProp_SetWaterInteractor(water, position, velocity, radius, submerged);
            return;
        }
    }
    MapProp_SetWaterInteractor(water, position, velocity, radius, 0.0f);
}

void MapDem_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity)
{
    if (water)
        MapProp_AddWaterRipple(water, position, radius, intensity);
}

#endif // MAP_PROPS_DEM_INL