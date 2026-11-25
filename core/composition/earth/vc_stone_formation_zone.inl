// ============================================================================
// vc_stone_formation_zone.inl — Hậu Thổ Trấn Nhạc Trận: AAA Octagonal Zone
//
// Strictly ground-hugging (y < 0.15m above terrain) for 35° camera readability:
// - 8 sculpted multi-faceted basalt monolith rim stones with stepped outer
//   buttresses, chamfered bevels, and carved central ridges (y <= 0.13m)
// - Connected octagonal 3D ground fissure cracks + subtle earthy interior
//   (alpha <= 0.28) with gentle pulse + grass-parting wind on ticks.
// - Zero bright yellow ring, zero vertical smoke wall obscuring characters.
// ============================================================================

#ifndef VC_STONE_FORMATION_ZONE_INL
#define VC_STONE_FORMATION_ZONE_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/composition/visual_composer.h"
#include "core/material/material_system.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VC_STONE_ZONE_MAX_INSTANCES 8

typedef struct {
    bool active;
    int id;
    Vector3 center;
    float radius;
    float elapsed;
    float duration;
    float pulse01;
    bool stopping;
    float fadeTimer;
} VC_StoneZoneInstance;

static VC_StoneZoneInstance s_stoneZones[VC_STONE_ZONE_MAX_INSTANCES];
static int s_stoneZoneNextId = 1;

static void StoneZone_EmitTri(Vector3 a, Vector3 b, Vector3 c) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    float len = Vector3Length(n);
    if (len > 1e-5f) n = Vector3Scale(n, 1.0f / len);
    else n = (Vector3){ 0.0f, 1.0f, 0.0f };
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
}

/* Sculpted multi-faceted basalt border monolith (stepped outer buttress + beveled ridge, y <= 0.13m) */
static void StoneZone_DrawSculptedBorderSlab(float length, float width, float height, int seed) {
    float hl = length * 0.5f;
    float hw = width * 0.5f;
    float th = fminf(0.13f, height); /* strictly < 0.15m */
    float midH = th * 0.62f;
    float j = ((seed & 1) ? 1.04f : 0.96f);

    /* Base octagonal footprint (6 vertices) */
    Vector3 base[6] = {
        { -hl * 0.96f, 0.0f, -hw * 0.65f },
        {  0.0f,       0.0f, -hw * 1.15f * j },
        {  hl * 0.96f, 0.0f, -hw * 0.65f },
        {  hl * 0.88f, 0.0f,  hw * 0.72f },
        {  0.0f,       0.0f,  hw * 0.95f },
        { -hl * 0.88f, 0.0f,  hw * 0.72f }
    };
    /* Chamfered mid tier */
    Vector3 mid[6] = {
        { -hl * 0.80f, midH, -hw * 0.45f },
        {  0.0f,       midH, -hw * 0.82f * j },
        {  hl * 0.80f, midH, -hw * 0.45f },
        {  hl * 0.74f, midH,  hw * 0.50f },
        {  0.0f,       midH,  hw * 0.66f },
        { -hl * 0.74f, midH,  hw * 0.50f }
    };
    /* Chiseled dorsal crest ridge */
    Vector3 ridgeL = { -hl * 0.52f, th, 0.0f };
    Vector3 ridgeC = {  0.0f,       th * 1.08f, -hw * 0.08f };
    Vector3 ridgeR = {  hl * 0.52f, th, 0.0f };

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        int ni = (i + 1) % 6;
        StoneZone_EmitTri(base[i], base[ni], mid[ni]);
        StoneZone_EmitTri(base[i], mid[ni], mid[i]);
    }
    /* Top chiseled facets */
    StoneZone_EmitTri(mid[0], mid[1], ridgeC);
    StoneZone_EmitTri(mid[0], ridgeC, ridgeL);
    StoneZone_EmitTri(mid[1], mid[2], ridgeR);
    StoneZone_EmitTri(mid[1], ridgeR, ridgeC);
    StoneZone_EmitTri(mid[2], mid[3], ridgeR);
    StoneZone_EmitTri(mid[3], mid[4], ridgeC);
    StoneZone_EmitTri(mid[3], ridgeC, ridgeR);
    StoneZone_EmitTri(mid[4], mid[5], ridgeL);
    StoneZone_EmitTri(mid[4], ridgeL, ridgeC);
    StoneZone_EmitTri(mid[5], mid[0], ridgeL);
    rlEnd();
}

int VFX_StoneFormationZone_Spawn(Vector3 center, float radius, float duration) {
    int slot = -1;
    for (int i = 0; i < VC_STONE_ZONE_MAX_INSTANCES; i++) {
        if (!s_stoneZones[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_StoneZoneInstance *inst = &s_stoneZones[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->id = s_stoneZoneNextId++;
    inst->center = center;
    inst->radius = (radius > 0.5f) ? radius : 4.0f;
    inst->elapsed = 0.0f;
    inst->duration = (duration > 0.2f) ? duration : 5.0f;
    inst->pulse01 = 1.0f;
    inst->stopping = false;
    inst->fadeTimer = 0.0f;

    /* Spawn conformal octagonal perimeter crack segments */
    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = 0.25f;
    ccfg.holdDuration = inst->duration - 0.5f;
    ccfg.fadeDuration = 0.45f;
    ccfg.veinGlowIntensity = 0.22f;
    ccfg.emitDust = false;
    ccfg.emitDebris = false;
    for (int s = 0; s < 8; s++) {
        float a0 = (float)s * (2.0f * PI / 8.0f);
        float a1 = (float)(s + 1) * (2.0f * PI / 8.0f);
        Vector3 p0 = { center.x + sinf(a0) * inst->radius * 0.94f, center.y, center.z + cosf(a0) * inst->radius * 0.94f };
        Vector3 p1 = { center.x + sinf(a1) * inst->radius * 0.94f, center.y, center.z + cosf(a1) * inst->radius * 0.94f };
        VFX_ComposeEarthCrackAB(p0, p1, 0.22f, &ccfg);
    }

    Wind_SpawnRadialBlast(center, inst->radius * 1.15f, 4.5f, 0.25f);
    return inst->id;
}

static VC_StoneZoneInstance *StoneZone_Find(int handle) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < VC_STONE_ZONE_MAX_INSTANCES; i++) {
        if (s_stoneZones[i].active && s_stoneZones[i].id == handle) {
            return &s_stoneZones[i];
        }
    }
    return NULL;
}

void VFX_StoneFormationZone_SetPulse(int handle, float pulse01) {
    VC_StoneZoneInstance *inst = StoneZone_Find(handle);
    if (!inst) return;
    if (pulse01 < 0.0f) pulse01 = 0.0f;
    if (pulse01 > 1.0f) pulse01 = 1.0f;
    inst->pulse01 = pulse01;
    if (pulse01 >= 0.85f) {
        Wind_SpawnRadialBlast(inst->center, inst->radius * 1.05f, 2.8f, 0.20f);
    }
}

void VFX_StoneFormationZone_Stop(int handle) {
    VC_StoneZoneInstance *inst = StoneZone_Find(handle);
    if (!inst) return;
    if (!inst->stopping) {
        inst->stopping = true;
        inst->fadeTimer = 0.35f;
    }
}

void VFX_StoneFormationZone_Kill(int handle) {
    VC_StoneZoneInstance *inst = StoneZone_Find(handle);
    if (inst) inst->active = false;
}

void VC_StoneFormationZone_Update(float dt) {
    for (int i = 0; i < VC_STONE_ZONE_MAX_INSTANCES; i++) {
        VC_StoneZoneInstance *inst = &s_stoneZones[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        inst->pulse01 = fmaxf(0.0f, inst->pulse01 - dt * 1.8f);
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping = true;
            inst->fadeTimer = 0.35f;
        }
    }
}

void VC_StoneFormationZone_Draw3D(Camera3D cam) {
    (void)cam;
    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);
    mat.params.baseColor = (Color){ 92, 82, 70, 255 };
    mat.params.rimStrength = 0.70f;
    mat.params.fresnelPower = 2.6f;
    mat.params.emissiveIntensity = 0.0f;
    mat.params.translucency = 0.0f;
    mat.vfxOutputEnabled = 0;

    for (int i = 0; i < VC_STONE_ZONE_MAX_INSTANCES; i++) {
        const VC_StoneZoneInstance *inst = &s_stoneZones[i];
        if (!inst->active) continue;

        float fade01 = inst->stopping ? fmaxf(0.0f, inst->fadeTimer / 0.35f) : 1.0f;
        if (fade01 <= 0.01f) continue;

        float r = inst->radius;
        float slabLen = 2.0f * r * tanf(PI / 8.0f) * 0.90f;

        /* 1. 8 sculpted low-profile octagonal monolith border slabs (staggered rise, y <= 0.13m) */
        Material_Begin(mat);
        for (int s = 0; s < 8; s++) {
            float localT = fmaxf(0.0f, inst->elapsed - (float)s * 0.025f);
            float rise01 = fminf(1.0f, localT / 0.20f);
            float vis = rise01 * fade01;
            if (vis <= 0.02f) continue;

            float angle = ((float)s + 0.5f) * (2.0f * PI / 8.0f);
            float midDist = r * cosf(PI / 8.0f);
            float sx = inst->center.x + sinf(angle) * midDist;
            float sz = inst->center.z + cosf(angle) * midDist;
            float sy = MapManager_GetGroundHeightAt(sx, sz) - 0.01f;
            float slabHeight = (0.095f + 0.025f * (float)(s & 1)) * vis;

            rlPushMatrix();
            rlTranslatef(sx, sy, sz);
            rlRotatef(angle * RAD2DEG, 0.0f, 1.0f, 0.0f);
            StoneZone_DrawSculptedBorderSlab(slabLen, 0.44f, slabHeight, s + inst->id);
            rlPopMatrix();
        }
        Material_End();

        /* 2. Terrain-draped SDF Bagua/Earth Rune Seal (C11 ground-layer, y <= 0.15m) */
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        Vector3 gp = { inst->center.x, MapManager_GetGroundHeightAt(inst->center.x, inst->center.z), inst->center.z };
        VFX_ComposeRuneCircle(gp, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_EARTH, r * 0.92f, t01, 2);
        if (inst->pulse01 > 0.15f) {
            VC_DrawDualRingBand(gp, r * (0.25f + 0.65f * (1.0f - inst->pulse01)),
                                r * (0.31f + 0.65f * (1.0f - inst->pulse01)), 0.05f, 16,
                                (Color){ 68, 52, 32, (unsigned char)(85.0f * inst->pulse01 * fade01) },
                                (Color){ 225, 178, 88, (unsigned char)(90.0f * inst->pulse01 * fade01) }, 0.0f);
        }
    }
}

#endif /* VC_STONE_FORMATION_ZONE_INL */
