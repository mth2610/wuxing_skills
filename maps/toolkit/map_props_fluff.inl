// Batched atmospheric pollen / dandelion fluff spores system
#ifndef MAP_PROPS_FLUFF_INL
#define MAP_PROPS_FLUFF_INL

#include "maps/toolkit/map_props.h"
#include "environment/environment_system.h"
#include "core/gfx_quality.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static inline float Fluff_Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)(*rng & 0x00ffffffu) * (1.0f / 16777216.0f);
}

static inline void Fluff_FastSinCos(float angleRad, float *restrict outSin, float *restrict outCos)
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

static inline int Fluff_GetActiveCount(int totalCount)
{
    GfxQuality q = GfxQuality_Get();
    if (q >= GFX_HIGH)
        return totalCount;
    if (q == GFX_MED)
        return (totalCount * 3 + 3) >> 2;
    return (totalCount + 1) >> 1;
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
        float dy = ((float)y - 7.5f) * (1.0f / 7.5f);
        float dy2 = dy * dy;
        for (int x = 0; x < 16; x++)
        {
            float dx = ((float)x - 7.5f) * (1.0f / 7.5f);
            float r2 = dx * dx + dy2;
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
        float sinT, cosT;
        Fluff_FastSinCos(theta, &sinT, &cosT);
        sys.particles[i].position = (Vector3){cosT * dist, h, sinT * dist};
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
    if (!fluff || !fluff->ready || !fluff->particles || dt <= 0.0f)
        return;

    const float windDirX = 0.78f;
    const float windDirY = -0.04f;
    const float windDirZ = 0.62f;
    const float time = fmodf((float)GetTime(), 628.31853f);
    const float tWave = time * 1.35f;
    const float tLift = time * 0.85f;
    const bool hasGround = (ground != NULL && ground->ready);

    // Bảng hướng tái sinh tiền tính toán cho 11 biến thể góc (triệt tiêu atan2f/sinf/cosf khi hạt bay ra ngoài vùng)
    static bool s_respawnLutReady = false;
    static float s_respawnCos[11];
    static float s_respawnSin[11];
    if (!s_respawnLutReady)
    {
        float baseAngle = atan2f(windDirZ, windDirX) + PI;
        for (int k = 0; k < 11; k++)
        {
            float angle = baseAngle + ((float)k - 5.0f) * 0.22f;
            Fluff_FastSinCos(angle, &s_respawnSin[k], &s_respawnCos[k]);
        }
        s_respawnLutReady = true;
    }

    int activeCount = Fluff_GetActiveCount(fluff->count);
    MapFluffParticle *restrict particles = fluff->particles;

    for (int i = 0; i < activeCount; i++)
    {
        MapFluffParticle *p = &particles[i];
        float wave, lift, tmpC;
        Fluff_FastSinCos(tWave + p->phase, &wave, &tmpC);
        Fluff_FastSinCos(tLift + p->phase * 1.5f, &tmpC, &lift);

        float spd = p->speed;
        p->position.x += (windDirX * spd + wave * 0.11f) * dt;
        p->position.y += (windDirY * spd + lift * 0.07f) * dt;
        p->position.z += (windDirZ * spd - wave * 0.09f) * dt;
        p->rot += p->rotSpeed * dt;

        float dx = p->position.x - cameraTarget.x;
        float dz = p->position.z - cameraTarget.z;
        float distSq = dx * dx + dz * dz;

        if (distSq > 256.0f || p->position.y < minGroundY)
        {
            int dirSlot = i % 11;
            float spawnDist = 14.5f + (float)(i % 7) * 0.25f;
            p->position.x = cameraTarget.x + s_respawnCos[dirSlot] * spawnDist;
            p->position.z = cameraTarget.z + s_respawnSin[dirSlot] * spawnDist;

            float gy = minGroundY;
            if (hasGround)
                gy = MapProp_SampleGroundHeight(ground, groundCenter, p->position.x, p->position.z);

            p->position.y = fmaxf(gy + 0.5f, minGroundY + 0.8f) + (float)(i % 5) * 0.65f;
        }
    }
}

void MapProp_DrawFluffSystem(const MapFluffSystem *fluff, Camera3D cam)
{
    if (!fluff || !fluff->ready || !fluff->particles || fluff->texture.id == 0)
        return;

    Vector3 forward = Vector3Subtract(cam.target, cam.position);
    float fwdLenSq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
    if (fwdLenSq < 1e-6f)
        return;
    forward = Vector3Scale(forward, 1.0f / sqrtf(fwdLenSq));

    Vector3 right = Vector3CrossProduct(forward, cam.up);
    float rightLenSq = right.x * right.x + right.y * right.y + right.z * right.z;
    if (rightLenSq < 1e-6f)
        return;
    right = Vector3Scale(right, 1.0f / sqrtf(rightLenSq));
    Vector3 up = Vector3CrossProduct(right, forward);

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float aspect = (float)sw / (float)(sh > 1 ? sh : 1);
    bool isOrtho = (cam.projection == CAMERA_ORTHOGRAPHIC);
    float tanHalfFovV = tanf(cam.fovy * DEG2RAD * 0.5f);
    float tanHalfFovH = tanHalfFovV * aspect;
    float orthoHalfV = cam.fovy * 0.5f;
    float orthoHalfH = orthoHalfV * aspect;

    Color sunColor = Environment_GetSunColor();
    unsigned char baseR = (unsigned char)fminf(255.0f, 248.0f * (sunColor.r * (1.0f / 255.0f)) * 1.08f);
    unsigned char baseG = (unsigned char)fminf(255.0f, 240.0f * (sunColor.g * (1.0f / 255.0f)) * 1.08f);
    unsigned char baseB = (unsigned char)fminf(255.0f, 212.0f * (sunColor.b * (1.0f / 255.0f)) * 1.02f);

    int activeCount = Fluff_GetActiveCount(fluff->count);
    const MapFluffParticle *restrict particles = fluff->particles;

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    rlSetTexture(fluff->texture.id);
    rlBegin(RL_QUADS);

    // Ngưỡng bình phương khoảng cách để bỏ qua sqrtf & SmoothStep ở vùng giữa [1.8m .. 12.0m]
    const float kNearFadeEndSq = 3.24f;   // 1.8^2
    const float kFarFadeStartSq = 144.0f; // 12.0^2

    for (int i = 0; i < activeCount; i++)
    {
        const MapFluffParticle *p = &particles[i];
        float dx = p->position.x - cam.position.x;
        float dy = p->position.y - cam.position.y;
        float dz = p->position.z - cam.position.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > 256.0f || distSq < 0.36f)
            continue;

        // Frustum culling từng hạt trước khi tính sin/cos và đẩy đỉnh
        float depth = dx * forward.x + dy * forward.y + dz * forward.z;
        float halfSize = p->size * 0.5f;
        if (depth < -halfSize)
            continue;

        float halfV = isOrtho ? orthoHalfV : (tanHalfFovV * (depth > 0.0f ? depth : 0.0f));
        float halfH = isOrtho ? orthoHalfH : (tanHalfFovH * (depth > 0.0f ? depth : 0.0f));

        float sideDist = fabsf(dx * right.x + dy * right.y + dz * right.z);
        if (sideDist > halfH + halfSize)
            continue;

        float vertDist = fabsf(dx * up.x + dy * up.y + dz * up.z);
        if (vertDist > halfV + halfSize)
            continue;

        float fade = p->opacity;
        if (distSq < kNearFadeEndSq)
        {
            fade *= MapMath_SmoothStep01(0.6f, 1.8f, sqrtf(distSq));
        }
        else if (distSq > kFarFadeStartSq)
        {
            fade *= (1.0f - MapMath_SmoothStep01(12.0f, 16.0f, sqrtf(distSq)));
        }

        unsigned char alpha = (unsigned char)(255.0f * fade);
        if (alpha < 4)
            continue;

        float sinR, cosR;
        Fluff_FastSinCos(p->rot, &sinR, &cosR);
        float c = cosR * halfSize;
        float s = sinR * halfSize;

        float rxx = right.x * c - up.x * s;
        float rxy = right.y * c - up.y * s;
        float rxz = right.z * c - up.z * s;

        float ryx = right.x * s + up.x * c;
        float ryy = right.y * s + up.y * c;
        float ryz = right.z * s + up.z * c;

        float px = p->position.x;
        float py = p->position.y;
        float pz = p->position.z;

        rlColor4ub(baseR, baseG, baseB, alpha);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(px - rxx - ryx, py - rxy - ryy, pz - rxz - ryz);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(px + rxx - ryx, py + rxy - ryy, pz + rxz - ryz);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(px + rxx + ryx, py + rxy + ryy, pz + rxz + ryz);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(px - rxx + ryx, py - rxy + ryy, pz - rxz + ryz);
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