// Batched atmospheric pollen / dandelion fluff spores system
#ifndef MAP_PROPS_FLUFF_INL
#define MAP_PROPS_FLUFF_INL

#include "maps/toolkit/map_props.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float Fluff_Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)*rng / 4294967296.0f;
}

MapFluffSystem MapProp_CreateFluffSystem(int count, unsigned int seed)
{
    MapFluffSystem sys = {0};
    if (count <= 0)
        return sys;

    sys.particles = (MapFluffParticle *)MemAlloc((unsigned int)count * sizeof(MapFluffParticle));
    if (!sys.particles)
        return sys;
    sys.count = count;

    // Generate soft circular dandelion spore texture (16x16)
    Image img = GenImageColor(16, 16, (Color){0, 0, 0, 0});
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            float dx = (x - 7.5f) / 7.5f;
            float dy = (y - 7.5f) / 7.5f;
            float r2 = dx * dx + dy * dy;
            if (r2 < 1.0f)
            {
                float a = powf(1.0f - sqrtf(r2), 1.7f);
                ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});
            }
        }
    }
    sys.texture = LoadTextureFromImage(img);
    SetTextureFilter(sys.texture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);

    unsigned int rng = seed ? seed : 0x6a1f29u;
    for (int i = 0; i < count; i++)
    {
        float theta = Fluff_Random01(&rng) * 2.0f * PI;
        float dist = 2.0f + sqrtf(Fluff_Random01(&rng)) * 14.5f;
        float h = 0.45f + Fluff_Random01(&rng) * 3.8f;
        sys.particles[i].position = (Vector3){cosf(theta) * dist, h, sinf(theta) * dist};
        sys.particles[i].phase = Fluff_Random01(&rng) * 2.0f * PI;
        sys.particles[i].size = 0.034f + 0.026f * Fluff_Random01(&rng);
        sys.particles[i].speed = 0.75f + 0.40f * Fluff_Random01(&rng);
        sys.particles[i].rot = Fluff_Random01(&rng) * 2.0f * PI;
        sys.particles[i].rotSpeed = (Fluff_Random01(&rng) - 0.5f) * 1.1f;
        sys.particles[i].opacity = 0.42f + 0.45f * Fluff_Random01(&rng);
    }

    sys.ready = true;
    return sys;
}

void MapProp_UpdateFluffSystem(MapFluffSystem *fluff, Vector3 cameraTarget,
                               const MapGroundSurface *ground, Vector3 groundCenter,
                               float minGroundY, float dt)
{
    if (!fluff || !fluff->ready || !fluff->particles)
        return;

    Vector3 windDir = {0.78f, -0.04f, 0.62f};
    float time = (float)GetTime();

    for (int i = 0; i < fluff->count; i++)
    {
        MapFluffParticle *p = &fluff->particles[i];
        float wave = sinf(time * 1.35f + p->phase);
        float lift = cosf(time * 0.85f + p->phase * 1.5f);

        p->position.x += (windDir.x * p->speed + wave * 0.11f) * dt;
        p->position.y += (windDir.y * p->speed + lift * 0.07f) * dt;
        p->position.z += (windDir.z * p->speed - wave * 0.09f) * dt;
        p->rot += p->rotSpeed * dt;

        float dx = p->position.x - cameraTarget.x;
        float dz = p->position.z - cameraTarget.z;
        float distSq = dx * dx + dz * dz;

        if (distSq > 256.0f || p->position.y < minGroundY)
        {
            float angle = (atan2f(windDir.z, windDir.x) + PI) + ((float)(i % 11) - 5.0f) * 0.22f;
            float spawnDist = 14.5f + (float)(i % 7) * 0.25f;
            p->position.x = cameraTarget.x + cosf(angle) * spawnDist;
            p->position.z = cameraTarget.z + sinf(angle) * spawnDist;

            float gy = minGroundY;
            if (ground && ground->ready)
                gy = MapProp_SampleGroundHeight(ground, groundCenter, p->position.x, p->position.z);

            p->position.y = fmaxf(gy + 0.5f, minGroundY + 0.8f) + ((float)(i % 5)) * 0.65f;
        }
    }
}

void MapProp_DrawFluffSystem(const MapFluffSystem *fluff, Camera3D cam)
{
    if (!fluff || !fluff->ready || !fluff->particles || fluff->texture.id == 0)
        return;

    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    Color sunColor = Environment_GetSunColor();
    unsigned char baseR = (unsigned char)fminf(255.0f, 248.0f * (sunColor.r / 255.0f) * 1.08f);
    unsigned char baseG = (unsigned char)fminf(255.0f, 240.0f * (sunColor.g / 255.0f) * 1.08f);
    unsigned char baseB = (unsigned char)fminf(255.0f, 212.0f * (sunColor.b / 255.0f) * 1.02f);

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    rlSetTexture(fluff->texture.id);
    rlBegin(RL_QUADS);

    for (int i = 0; i < fluff->count; i++)
    {
        const MapFluffParticle *p = &fluff->particles[i];
        float dx = p->position.x - cam.position.x;
        float dy = p->position.y - cam.position.y;
        float dz = p->position.z - cam.position.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > 256.0f || distSq < 0.36f)
            continue;

        float camDist = sqrtf(distSq);
        float fade = p->opacity * MapMath_SmoothStep01(0.6f, 1.8f, camDist) *
                     (1.0f - MapMath_SmoothStep01(12.0f, 16.0f, camDist));
        unsigned char alpha = (unsigned char)(255.0f * fade);
        if (alpha < 4)
            continue;

        float halfSize = p->size * 0.5f;
        float c = cosf(p->rot) * halfSize;
        float s = sinf(p->rot) * halfSize;

        Vector3 rx = {right.x * c - up.x * s, right.y * c - up.y * s, right.z * c - up.z * s};
        Vector3 ry = {right.x * s + up.x * c, right.y * s + up.y * c, right.z * s + up.z * c};

        Vector3 v0 = {p->position.x - rx.x - ry.x, p->position.y - rx.y - ry.y, p->position.z - rx.z - ry.z};
        Vector3 v1 = {p->position.x + rx.x - ry.x, p->position.y + rx.y - ry.y, p->position.z + rx.z - ry.z};
        Vector3 v2 = {p->position.x + rx.x + ry.x, p->position.y + rx.y + ry.y, p->position.z + rx.z + ry.z};
        Vector3 v3 = {p->position.x - rx.x + ry.x, p->position.y - rx.y + ry.y, p->position.z - rx.z + ry.z};

        rlColor4ub(baseR, baseG, baseB, alpha);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(v0.x, v0.y, v0.z);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }

    rlEnd();
    rlSetTexture(0);

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void MapProp_UnloadFluffSystem(MapFluffSystem *fluff)
{
    if (!fluff)
        return;
    if (fluff->particles)
    {
        MemFree(fluff->particles);
        fluff->particles = NULL;
    }
    if (fluff->texture.id > 0)
    {
        UnloadTexture(fluff->texture);
        fluff->texture = (Texture2D){0};
    }
    fluff->count = 0;
    fluff->ready = false;
}

#endif // MAP_PROPS_FLUFF_INL
