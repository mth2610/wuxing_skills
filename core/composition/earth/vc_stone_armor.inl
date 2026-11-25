// ============================================================================
// vc_stone_armor.inl — Bàn Thạch Bát Cực Quyền: AAA Sculpted Stone Plate Armor
//
// Two modes:
// - VFX_STONE_ARMOR_FULL_BODY: 6 chiseled, multi-faceted basalt armor plates
//   (layered pauldrons, angled forearm vambraces, chiseled chest & back plates)
//   with staggered tectonic snap-in, subtle earthy-ochre seam trim, and
//   reactive outward shatter + camera micro-shake as shield HP drops.
// - VFX_STONE_ARMOR_SHOULDER_RAM: Dual overlapping jagged basalt wedge plates
//   mounted on the leading shoulder for Thiết Sơn Khảo.
//
// Clean 35° isometric read: proper diffuse + Fresnel shading (MAT_ROCK), zero
// bright golden ring or smoke wall obscuring the character.
// ============================================================================

#ifndef VC_STONE_ARMOR_INL
#define VC_STONE_ARMOR_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/composition/visual_composer.h"
#include "core/material/material_system.h"
#include "post_process/camera_fx.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VC_STONE_ARMOR_MAX_INSTANCES 12
#define VC_STONE_ARMOR_MAX_PLATES    6

typedef struct {
    bool active;
    int id;
    VFX_StoneArmorMode mode;
    Vector3 pos;
    Vector3 forward;
    float scale;
    float elapsed;
    float duration;
    bool stopping;
    float fadeTimer;
    int maxPlates;
    int activePlates;
} VC_StoneArmorInstance;

static VC_StoneArmorInstance s_stoneArmor[VC_STONE_ARMOR_MAX_INSTANCES];
static int s_stoneArmorNextId = 1;

static void StoneArmor_EmitTri(Vector3 a, Vector3 b, Vector3 c) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    float len = Vector3Length(n);
    if (len > 1e-5f) n = Vector3Scale(n, 1.0f / len);
    else n = (Vector3){ 0.0f, 1.0f, 0.0f };
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
}

/* Chiseled 2-tier faceted basalt armor plate (octagonal outer bevel + raised ridge spine) */
static void StoneArmor_DrawSculptedPlate(float w, float h, float d, int seed) {
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float hd = d * 0.5f;
    float j0 = ((seed & 1) ? 0.92f : 1.06f);
    float j1 = ((seed & 2) ? 1.05f : 0.93f);

    /* 6-point chiseled silhouette on back plane (-hd) and beveled front ridge (+hd) */
    Vector3 back[6] = {
        {  0.0f,       hh * 1.08f * j0, -hd },
        {  hw * 0.95f, hh * 0.48f,      -hd },
        {  hw * 0.82f,-hh * 0.68f * j1, -hd },
        {  0.0f,      -hh * 1.04f,      -hd },
        { -hw * 0.82f,-hh * 0.68f * j0, -hd },
        { -hw * 0.95f, hh * 0.48f,      -hd }
    };
    Vector3 rim[6] = {
        {  0.0f,       hh * 0.84f * j0,  hd * 0.45f },
        {  hw * 0.72f, hh * 0.36f,       hd * 0.45f },
        {  hw * 0.60f,-hh * 0.52f * j1,  hd * 0.45f },
        {  0.0f,      -hh * 0.80f,       hd * 0.45f },
        { -hw * 0.60f,-hh * 0.52f * j0,  hd * 0.45f },
        { -hw * 0.72f, hh * 0.36f,       hd * 0.45f }
    };
    Vector3 spineTop = { 0.0f,  hh * 0.38f, hd * 1.25f };
    Vector3 spineBot = { 0.0f, -hh * 0.38f, hd * 1.15f };
    Vector3 backCenter = { 0.0f, 0.0f, -hd };

    rlBegin(RL_TRIANGLES);
    /* Outer beveled skirt */
    for (int i = 0; i < 6; i++) {
        int ni = (i + 1) % 6;
        StoneArmor_EmitTri(back[i], back[ni], rim[ni]);
        StoneArmor_EmitTri(back[i], rim[ni], rim[i]);
        StoneArmor_EmitTri(backCenter, back[ni], back[i]);
    }
    /* Chiseled front ridge facets */
    StoneArmor_EmitTri(rim[0], rim[1], spineTop);
    StoneArmor_EmitTri(rim[5], rim[0], spineTop);
    StoneArmor_EmitTri(rim[1], rim[2], spineBot);
    StoneArmor_EmitTri(rim[1], spineBot, spineTop);
    StoneArmor_EmitTri(rim[2], rim[3], spineBot);
    StoneArmor_EmitTri(rim[3], rim[4], spineBot);
    StoneArmor_EmitTri(rim[4], rim[5], spineTop);
    StoneArmor_EmitTri(rim[4], spineTop, spineBot);
    rlEnd();
}

static Vector3 StoneArmor_GetPlateWorldPos(const VC_StoneArmorInstance *inst, int idx, float *outYawDeg, float *outPitchDeg) {
    Vector3 fwd = inst->forward;
    fwd.y = 0.0f;
    float fl = Vector3Length(fwd);
    if (fl < 0.001f) fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    else fwd = Vector3Scale(fwd, 1.0f / fl);
    float baseYaw = atan2f(fwd.x, fwd.z) * RAD2DEG;

    if (inst->mode == VFX_STONE_ARMOR_SHOULDER_RAM) {
        /* Dual overlapping layered basalt wedge plates on leading shoulder */
        float fwdOff = (idx == 0) ? 0.42f : 0.28f;
        float side   = (idx == 0) ? -0.18f : -0.32f;
        float yOff   = (idx == 0) ? 1.08f : 1.24f;
        Vector3 right = { fwd.z, 0.0f, -fwd.x };
        Vector3 p = Vector3Add(inst->pos, Vector3Scale(fwd, fwdOff * inst->scale));
        p = Vector3Add(p, Vector3Scale(right, side * inst->scale));
        p.y += yOff * inst->scale;
        if (outYawDeg) *outYawDeg = baseYaw + ((idx == 0) ? -12.0f : -28.0f);
        if (outPitchDeg) *outPitchDeg = (idx == 0) ? -16.0f : -24.0f;
        return p;
    }

    /* FULL_BODY: 6 sculpted plates (chest, L/R pauldrons, L/R forearm bracers, back) */
    static const float anglesDeg[6] = { 0.0f, 64.0f, -64.0f, 122.0f, -122.0f, 180.0f };
    static const float heights[6]   = { 1.04f, 1.26f, 1.26f, 0.90f, 0.90f, 1.08f };
    static const float radii[6]     = { 0.43f, 0.49f, 0.49f, 0.46f, 0.46f, 0.42f };
    static const float pitches[6]   = { -8.0f, -20.0f, -20.0f, 12.0f, 12.0f, 8.0f };

    int k = idx % 6;
    float orbitSpin = inst->elapsed * 16.0f;
    float yaw = baseYaw + anglesDeg[k] + orbitSpin;
    float rad = yaw * DEG2RAD;
    float bob = sinf(inst->elapsed * 3.0f + (float)k * 1.05f) * 0.022f;

    /* Staggered tectonic rise-and-settle on cast */
    float localT = fmaxf(0.0f, inst->elapsed - (float)k * 0.025f);
    float rise = fminf(1.0f, localT / 0.14f);
    float yDrop = (1.0f - rise) * -0.45f;

    Vector3 p = inst->pos;
    p.x += sinf(rad) * radii[k] * inst->scale;
    p.z += cosf(rad) * radii[k] * inst->scale;
    p.y += (heights[k] + bob + yDrop) * inst->scale;
    if (outYawDeg) *outYawDeg = yaw;
    if (outPitchDeg) *outPitchDeg = pitches[k];
    return p;
}

int VFX_StoneArmor_Spawn(Vector3 pos, Vector3 forward, VFX_StoneArmorMode mode, float scale, float duration) {
    int slot = -1;
    for (int i = 0; i < VC_STONE_ARMOR_MAX_INSTANCES; i++) {
        if (!s_stoneArmor[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_StoneArmorInstance *inst = &s_stoneArmor[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->id = s_stoneArmorNextId++;
    inst->mode = mode;
    inst->pos = pos;
    inst->forward = forward;
    inst->scale = (scale > 0.1f) ? scale : 1.0f;
    inst->elapsed = 0.0f;
    inst->duration = (duration > 0.05f) ? duration : 4.5f;
    inst->stopping = false;
    inst->fadeTimer = 0.0f;
    inst->maxPlates = (mode == VFX_STONE_ARMOR_SHOULDER_RAM) ? 2 : 6;
    inst->activePlates = inst->maxPlates;

    /* Initial ground-up tectonic snap */
    VFX_ComposeDebrisShards(pos, (Vector3){ 0.0f, 2.4f, 0.0f }, VC_MAT_EARTH, 0.038f * inst->scale, 5);
    return inst->id;
}

static VC_StoneArmorInstance *StoneArmor_Find(int handle) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < VC_STONE_ARMOR_MAX_INSTANCES; i++) {
        if (s_stoneArmor[i].active && s_stoneArmor[i].id == handle) {
            return &s_stoneArmor[i];
        }
    }
    return NULL;
}

void VFX_StoneArmor_SetTransform(int handle, Vector3 pos, Vector3 forward) {
    VC_StoneArmorInstance *inst = StoneArmor_Find(handle);
    if (!inst) return;
    inst->pos = pos;
    inst->forward = forward;
}

void VFX_StoneArmor_SetActivePlates(int handle, int activePlates) {
    VC_StoneArmorInstance *inst = StoneArmor_Find(handle);
    if (!inst) return;
    if (activePlates < 0) activePlates = 0;
    if (activePlates > inst->maxPlates) activePlates = inst->maxPlates;

    if (activePlates < inst->activePlates) {
        for (int p = activePlates; p < inst->activePlates; p++) {
            float yaw = 0.0f, pitch = 0.0f;
            Vector3 platePos = StoneArmor_GetPlateWorldPos(inst, p, &yaw, &pitch);
            Vector3 outDir = Vector3Subtract(platePos, inst->pos);
            outDir.y = 0.55f;
            float l = Vector3Length(outDir);
            if (l > 0.001f) outDir = Vector3Scale(outDir, 4.4f / l);
            VFX_ComposeDebrisShards(platePos, outDir, VC_MAT_EARTH, 0.052f * inst->scale, 7);
            VFX_ComposeImpactDustVariant(platePos, VC_MAT_EARTH, 0.45f * inst->scale, 0.38f, VFX_IMPACT_DUST_VARIANT_SHADER_DUST);
        }
    }
    inst->activePlates = activePlates;
}

void VFX_StoneArmor_Stop(int handle) {
    VC_StoneArmorInstance *inst = StoneArmor_Find(handle);
    if (!inst) return;
    if (!inst->stopping) {
        inst->stopping = true;
        inst->fadeTimer = 0.22f;
    }
}

void VFX_StoneArmor_Kill(int handle) {
    VC_StoneArmorInstance *inst = StoneArmor_Find(handle);
    if (inst) inst->active = false;
}

void VC_StoneArmor_Update(float dt) {
    for (int i = 0; i < VC_STONE_ARMOR_MAX_INSTANCES; i++) {
        VC_StoneArmorInstance *inst = &s_stoneArmor[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            VFX_StoneArmor_SetActivePlates(inst->id, 0);
            inst->active = false;
        }
    }
}

void VC_StoneArmor_Draw3D(Camera3D cam) {
    (void)cam;
    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);
    mat.params.rimStrength = 0.85f;
    mat.params.fresnelPower = 2.8f;
    mat.params.emissiveIntensity = 0.0f;
    mat.params.translucency = 0.0f;
    mat.vfxOutputEnabled = 0;

    for (int i = 0; i < VC_STONE_ARMOR_MAX_INSTANCES; i++) {
        const VC_StoneArmorInstance *inst = &s_stoneArmor[i];
        if (!inst->active || inst->activePlates <= 0) continue;

        float fadeScale = inst->stopping ? fmaxf(0.0f, inst->fadeTimer / 0.22f) : 1.0f;
        if (fadeScale <= 0.02f) continue;

        mat.params.baseColor = (inst->mode == VFX_STONE_ARMOR_SHOULDER_RAM)
            ? (Color){ 88, 78, 68, 255 }
            : (Color){ 102, 88, 74, 255 };

        Material_Begin(mat);
        for (int p = 0; p < inst->activePlates; p++) {
            float yaw = 0.0f, pitch = 0.0f;
            Vector3 pPos = StoneArmor_GetPlateWorldPos(inst, p, &yaw, &pitch);

            /* Mechanical overshoot bounce on snap-in */
            float localT = fmaxf(0.0f, inst->elapsed - (float)p * 0.025f);
            float tNorm = fminf(1.0f, localT / 0.16f);
            float overshoot = (tNorm < 1.0f) ? (1.0f + 0.14f * sinf(tNorm * PI)) : 1.0f;
            float s = inst->scale * fminf(1.0f, tNorm * 1.4f) * overshoot * fadeScale;
            if (s <= 0.02f) continue;

            float pw = (inst->mode == VFX_STONE_ARMOR_SHOULDER_RAM) ? ((p == 0) ? 0.62f : 0.48f) * s : 0.36f * s;
            float ph = (inst->mode == VFX_STONE_ARMOR_SHOULDER_RAM) ? ((p == 0) ? 0.68f : 0.54f) * s : 0.44f * s;
            float pd = (inst->mode == VFX_STONE_ARMOR_SHOULDER_RAM) ? 0.16f * s : 0.085f * s;

            rlPushMatrix();
            rlTranslatef(pPos.x, pPos.y, pPos.z);
            rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
            rlRotatef(pitch, 1.0f, 0.0f, 0.0f);
            StoneArmor_DrawSculptedPlate(pw, ph, pd, p + inst->id);
            rlPopMatrix();
        }
        Material_End();
    }
}

#endif /* VC_STONE_ARMOR_INL */
