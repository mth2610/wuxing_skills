#include "skills/earth/btbc_khai_son_thach_dan_skill/btbc_khai_son_thach_dan_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/material/material_system.h"
#include "core/wind/wind_system.h"
#include "post_process/camera_fx.h"
#include "maps/map_manager.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_KSTD_INST 6
#define KSTD_SPEED    16.0f
#define KSTD_RANGE    9.5f
#define KSTD_RADIUS   0.44f

typedef struct {
    bool    active;
    bool    launched;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    Vector3 pos;
    float   elapsed;
    float   castSecs;
    float   travelled;
    float   wakeAccum;
    float   damage;
    float   sizeScale;
    float   spinAngle;
} KhaiSonInst;

static KhaiSonInst s_inst[MAX_KSTD_INST];
static int s_skillIndex = -1;

static void EmitStoneTri(Vector3 a, Vector3 b, Vector3 c) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    float l = Vector3Length(n);
    n = (l > 1e-5f) ? Vector3Scale(n, 1.0f / l) : (Vector3){ 0.0f, 1.0f, 0.0f };
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
}

/* Chiseled 6-sided double-beveled basalt projectile slab */
static void DrawSculptedStoneDisc(float r, float thickness) {
    float ht = thickness * 0.5f;
    Vector3 rim[6];
    for (int i = 0; i < 6; i++) {
        float ang = (float)i * (2.0f * PI / 6.0f);
        float jit = (i & 1) ? 0.88f : 1.08f;
        rim[i] = (Vector3){ cosf(ang) * r * jit, 0.0f, sinf(ang) * r * jit };
    }
    Vector3 topApex = {  0.04f * r,  ht * 1.35f,  0.02f * r };
    Vector3 botApex = { -0.04f * r, -ht * 1.15f, -0.02f * r };

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        int ni = (i + 1) % 6;
        EmitStoneTri(rim[i], rim[ni], topApex);
        EmitStoneTri(rim[ni], rim[i], botApex);
    }
    rlEnd();
}

static void ShatterSlabAt(Vector3 pos, Vector3 dir, float scale) {
    Vector3 vel = { -dir.x * 2.5f, 3.2f, -dir.z * 2.5f };
    VFX_ComposeDebrisShards(pos, vel, VC_MAT_EARTH, 0.058f * scale, 6);
    VFX_ComposeImpactDustVariant(pos, VC_MAT_EARTH, 0.68f * scale, 0.45f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    VFX_ComposeGroundWave(pos, VC_MAT_EARTH, 1.15f * scale, 0.22f, VFX_GroundHeightFromMap, NULL);

    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = 0.12f;
    ccfg.holdDuration = 0.90f;
    ccfg.fadeDuration = 0.45f;
    ccfg.veinGlowIntensity = 0.18f;
    VFX_ComposeEarthCrackRadial(pos, 1.05f * scale, &ccfg);
    Wind_SpawnRadialBlast(pos, 1.6f * scale, 4.0f, 0.18f);
    CameraFX_Shake(0.18f);
}

static void AbortBtbcKhaiSon(int agentId) {
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcKhaiSon(int agentId) {
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcKhaiSonThachDanSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_KSTD_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("BTBC_KHAI_SON_THACH_DAN");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_KHAI_SON_THACH_DAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcKhaiSon);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcKhaiSon);
    }
}

void CastBtbcKhaiSonThachDanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_KHAI_SON_THACH_DAN");
    float baseDmg = def ? def->baseDamage : 17.5f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    KhaiSonInst *inst = &s_inst[slot];
    inst->active = true;
    inst->launched = false;
    inst->ownerAgentId = agentId;
    inst->origin = startPos;
    inst->dir = dir;
    inst->pos = Vector3Add(startPos, Vector3Scale(dir, 0.65f));
    inst->pos.y = MapManager_GetGroundHeightAt(inst->pos.x, inst->pos.z) + 0.10f;
    inst->elapsed = 0.0f;
    inst->castSecs = 0.18f;
    inst->travelled = 0.0f;
    inst->wakeAccum = 0.0f;
    inst->damage = baseDmg * pveMult;
    inst->sizeScale = scale;
    inst->spinAngle = 0.0f;

    /* Heel stomp: small ground crack + dust ring + rising pebbles */
    Skill_RequestAnim(agentId, "mma_kick", 0.18f);
    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = 0.12f;
    ccfg.holdDuration = 0.65f;
    ccfg.fadeDuration = 0.35f;
    ccfg.veinGlowIntensity = 0.16f;
    VFX_ComposeEarthCrackRadial(inst->pos, 0.85f * scale, &ccfg);
    VFX_ComposeGroundDustRing(inst->pos, VC_MAT_EARTH, 0.55f * scale, 0.40f);
    VFX_ComposeDebrisShards(inst->pos, (Vector3){ 0.0f, 2.6f, 0.0f }, VC_MAT_EARTH, 0.038f * scale, 5);
    Wind_SpawnRadialBlast(inst->pos, 1.2f * scale, 3.5f, 0.15f);
}

void UpdateBtbcKhaiSonThachDanSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    const ClashEvent *events = NULL;
    int eventCount = Combat_PeekEvents(&events);

    for (int i = 0; i < MAX_KSTD_INST; i++) {
        KhaiSonInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->launched) {
            float t = fminf(1.0f, inst->elapsed / inst->castSecs);
            float gy = MapManager_GetGroundHeightAt(inst->pos.x, inst->pos.z);
            inst->pos.y = gy + 0.10f + 0.88f * sinf(t * (PI * 0.5f));
            if (inst->elapsed >= inst->castSecs) {
                inst->launched = true;
                Skill_RequestAnim(inst->ownerAgentId, "hook_punch", 0.25f);
                VFX_ComposeImpactDustVariant(inst->pos, VC_MAT_EARTH, 0.58f * inst->sizeScale, 0.45f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                Wind_SpawnGust(inst->pos, inst->dir, 1.4f * inst->sizeScale, 8.0f, 0.16f);
            }
            continue;
        }

        int myId = (s_skillIndex >= 0) ? (s_skillIndex * 1000 + i) : (8100 + i);
        bool destroyed = false;
        for (int e = 0; e < eventCount; e++) {
            if (events[e].skillInstanceId != myId) continue;
            if (events[e].outcome == CLASH_HIT_AGENT) {
                VoHoc_ApplyEarthMark(inst->ownerAgentId, events[e].otherAgentId, 1);
                ShatterSlabAt(events[e].clashPoint, inst->dir, inst->sizeScale);
                destroyed = true;
                break;
            } else if (events[e].outcome == CLASH_B_WINS || events[e].outcome == CLASH_MUTUAL_DESTROY) {
                ShatterSlabAt(events[e].clashPoint, inst->dir, inst->sizeScale);
                destroyed = true;
                break;
            }
        }
        if (destroyed) {
            inst->active = false;
            continue;
        }

        float step = KSTD_SPEED * dt;
        inst->pos = Vector3Add(inst->pos, Vector3Scale(inst->dir, step));
        float gy = MapManager_GetGroundHeightAt(inst->pos.x, inst->pos.z);
        inst->pos.y = gy + 0.95f;
        inst->travelled += step;
        inst->spinAngle += 620.0f * dt;

        /* Continuous dust & gravel wake below flying stone */
        inst->wakeAccum += dt;
        if (inst->wakeAccum >= 0.045f) {
            inst->wakeAccum = 0.0f;
            Vector3 gPt = { inst->pos.x, gy + 0.06f, inst->pos.z };
            VFX_ComposeImpactDustVariant(gPt, VC_MAT_EARTH, 0.34f * inst->sizeScale, 0.25f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            VFX_ComposeDebrisShards(inst->pos, (Vector3){ -inst->dir.x * 1.4f, 0.8f, -inst->dir.z * 1.4f },
                                    VC_MAT_EARTH, 0.024f * inst->sizeScale, 2);
            Wind_SpawnGust(gPt, inst->dir, 1.1f * inst->sizeScale, 6.0f, 0.12f);
        }

        if (inst->travelled >= KSTD_RANGE * inst->sizeScale) {
            ShatterSlabAt(inst->pos, inst->dir, inst->sizeScale);
            inst->active = false;
            continue;
        }

        Combat_SubmitProjectile(inst->ownerAgentId, ELEM_EARTH, inst->pos,
                                KSTD_RADIUS * inst->sizeScale, inst->damage, 1.8f, myId);
    }
}

void DrawBtbcKhaiSonThachDanSkill(void) {
    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);
    mat.params.baseColor = (Color){ 104, 90, 74, 255 }; // lint: allow-color
    mat.params.rimStrength = 0.78f;
    mat.params.fresnelPower = 2.6f;
    mat.params.emissiveIntensity = 0.0f;
    mat.vfxOutputEnabled = 0;

    bool began = false;
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        const KhaiSonInst *inst = &s_inst[i];
        if (!inst->active) continue;
        if (!began) { Material_Begin(mat); began = true; }

        float yaw = atan2f(inst->dir.x, inst->dir.z) * RAD2DEG;
        float s = inst->sizeScale;
        rlPushMatrix();
        rlTranslatef(inst->pos.x, inst->pos.y, inst->pos.z);
        rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
        if (inst->launched) rlRotatef(inst->spinAngle, 0.0f, 0.0f, 1.0f);
        DrawSculptedStoneDisc(0.32f * s, 0.18f * s);
        rlPopMatrix();
    }
    if (began) Material_End();
}

void UnloadBtbcKhaiSonThachDanSkill(void) {
    for (int i = 0; i < MAX_KSTD_INST; i++) s_inst[i].active = false;
}

bool IsBtbcKhaiSonThachDanSkillCoiling(void) {
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        if (s_inst[i].active) return true;
    }
    return false;
}

int GetBtbcKhaiSonThachDanSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    if (!outProjectiles || maxProjectiles <= 0) return 0;
    for (int i = 0; i < MAX_KSTD_INST && count < maxProjectiles; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            outProjectiles[count].position = s_inst[i].pos;
            outProjectiles[count].radius = KSTD_RADIUS * s_inst[i].sizeScale;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateBtbcKhaiSonThachDanProjectile(int index) {
    int activeIdx = 0;
    for (int i = 0; i < MAX_KSTD_INST; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            if (activeIdx == index) {
                ShatterSlabAt(s_inst[i].pos, s_inst[i].dir, s_inst[i].sizeScale);
                s_inst[i].active = false;
                return;
            }
            activeIdx++;
        }
    }
}
