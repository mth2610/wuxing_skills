#include "skills/water/ttqp_nghich_luu_cam_na_skill/ttqp_nghich_luu_cam_na_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/liquid/liquid_swe.h"
#include "core/liquid/liquid_impact.h"
#include "core/composition/visual_composer.h"
#include "core/status_vfx.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_NLCN_INST 4

typedef struct {
    bool    active;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    Vector3 headPos;
    float   travelled;
    float   maxRange;
    float   elapsed;
    float   duration;
    float   damage;
    float   sizeScale;
    bool    impacted;
    int     sweHandle;
    int     tetherHandle;
    int     pulledTargetId;
} NghichLuuInst;

static NghichLuuInst s_inst[MAX_NLCN_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpNghichLuuCamNa)

static void SpawnPullSplashSSF(Vector3 pos, Vector3 pullDir, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = Vector3Normalize((Vector3){ pullDir.x, 0.35f, pullDir.z });
    ev.initialVelocity = (Vector3){ pullDir.x * 7.5f, 2.8f, pullDir.z * 7.5f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static void AbortTtqpNghichLuu(int agentId) {
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            if (s_inst[i].tetherHandle > 0) VFX_HydroTether_Kill(s_inst[i].tetherHandle);
            s_inst[i].tetherHandle = 0;
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpNghichLuu(int agentId) {
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpNghichLuuCamNaSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_NGHICH_LUU_CAM_NA");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_NGHICH_LUU_CAM_NA");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpNghichLuu);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpNghichLuu);
    }
}

void CastTtqpNghichLuuCamNaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_inst[0].sweHandle >= 0) LiquidSWE_Kill(s_inst[0].sweHandle);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_NGHICH_LUU_CAM_NA");
    float baseDmg = def ? def->baseDamage : 18.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float dist = Vector3Length(dir);
    if (dist < 0.01f) dir = (Vector3){ 0.0f, 0.0f, 1.0f };
    else dir = Vector3Scale(dir, 1.0f / dist);
    float range = 8.0f * scale;
    Vector3 endPos = Vector3Add(startPos, Vector3Scale(dir, range));

    Skill_RequestAnim(agentId, "wide_arm_spell_casting", 0.35f);

    NghichLuuInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->origin = startPos;
    inst->dir = dir;
    inst->headPos = Vector3Add(startPos, Vector3Scale(dir, 0.7f));
    inst->travelled = 0.0f;
    inst->maxRange = range;
    inst->elapsed = 0.0f;
    inst->duration = 1.35f;
    inst->damage = baseDmg;
    inst->sizeScale = scale;
    inst->impacted = false;

    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_OCTOPUS_FORM;
    desc.source = startPos;
    desc.target = endPos;
    desc.waveHeight = 0.72f * scale;
    desc.waveSpeed = 14.0f;
    desc.width = 5.2f * scale;
    desc.length = range;
    desc.duration = inst->duration;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.0f;
    desc.fastMartialStrike = true;
    inst->sweHandle = LiquidSWE_Spawn(&desc);
    inst->pulledTargetId = -1;
    Vector3 castHand = { startPos.x, startPos.y + 1.05f, startPos.z };
    Vector3 tipPos   = { inst->headPos.x, inst->headPos.y + 0.95f, inst->headPos.z };
    inst->tetherHandle = VFX_HydroTether_Spawn(castHand, tipPos, 0.14f * scale, inst->duration);

    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpNghichLuuCamNaSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    const ClashEvent *events = NULL;
    int evCount = Combat_PeekEvents(&events);
    int baseId = (s_skillIndex >= 0 ? s_skillIndex : 91) * 1000;

    for (int i = 0; i < MAX_NLCN_INST; i++) {
        NghichLuuInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        int instId = baseId + i;
        if (!inst->impacted) {
            for (int e = 0; e < evCount; e++) {
                if (events[e].skillInstanceId != instId) continue;
                if (events[e].outcome == CLASH_B_WINS || events[e].outcome == CLASH_MUTUAL_DESTROY) {
                    inst->impacted = true;
                    SpawnPullSplashSSF(events[e].clashPoint, Vector3Negate(inst->dir), 0.65f, 1.0f * inst->sizeScale);
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                    break;
                } else if (events[e].outcome == CLASH_HIT_AGENT) {
                    inst->impacted = true;
                    inst->pulledTargetId = events[e].otherAgentId;
                    Skill_RequestAnim(inst->ownerAgentId, "elbow_punch", 0.30f);
                    Vector3 pullDest = inst->origin;
                    const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
                    if (owner) pullDest = owner->position;

                    /* Small-radius grab burst around hit point */
                    int ids[12];
                    int cnt = Entity_GetNearbyTargets(events[e].clashPoint, 1.5f * inst->sizeScale, ids, 12);
                    for (int k = 0; k < cnt; k++) {
                        if (!Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) continue;
                        const Agent *t = Entity_GetAgent(ids[k]);
                        if (!t) continue;
                        Vector3 toCaster = Vector3Subtract(pullDest, t->position);
                        toCaster.y = 0.0f;
                        float d = Vector3Length(toCaster);
                        Vector3 stopPos = t->position;
                        if (d > 1.4f) {
                            float pullDist = d - 1.4f;
                            if (pullDist > 4.0f) pullDist = 4.0f;
                            stopPos = Vector3Add(t->position, Vector3Scale(toCaster, pullDist / d));
                        }
                        if (ids[k] != events[e].otherAgentId) {
                            Entity_ApplyDamageFrom(ids[k], inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0 });
                        }
                        Entity_ApplyPull(ids[k], stopPos, 11.5f, 0.40f);
                        Entity_ApplyRoot(ids[k], 0.80f);
                        StatusVFX_Attach(ids[k], EFFECT_PRESET_WATER_SPLASH, 0.85f);
                        VoHoc_ApplyWaterMark(inst->ownerAgentId, ids[k], 1);
                    }
                    SpawnPullSplashSSF(events[e].clashPoint, Vector3Negate(inst->dir), 0.80f, 1.15f * inst->sizeScale);
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                    break;
                }
            }
        }

        /* Traveling tentacle grab (18 m/s) from caster so enemy can dodge or clash */
        if (!inst->impacted) {
            float step = 18.0f * dt;
            inst->travelled += step;
            inst->headPos = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->travelled));
            if (inst->travelled >= inst->maxRange) {
                inst->impacted = true;
                SpawnPullSplashSSF(inst->headPos, Vector3Negate(inst->dir), 0.45f, 0.85f * inst->sizeScale);
            } else {
                Vector3 submitPos = { inst->headPos.x, inst->headPos.y + 0.8f, inst->headPos.z };
                Combat_SubmitProjectile(inst->ownerAgentId, ELEM_WATER, submitPos,
                                        0.90f * inst->sizeScale, inst->damage, 0.0f, instId);
            }
        }

        if (inst->tetherHandle > 0) {
            const Agent *own = Entity_GetAgent(inst->ownerAgentId);
            Vector3 fromP = own ? (Vector3){ own->position.x, own->position.y + 1.05f, own->position.z }
                                : (Vector3){ inst->origin.x, inst->origin.y + 1.05f, inst->origin.z };
            Vector3 toP = { inst->headPos.x, inst->headPos.y + 0.95f, inst->headPos.z };
            if (inst->impacted && inst->pulledTargetId >= 0) {
                const Agent *pt = Entity_GetAgent(inst->pulledTargetId);
                if (pt) toP = (Vector3){ pt->position.x, pt->position.y + 0.95f, pt->position.z };
            }
            VFX_HydroTether_SetEndpoints(inst->tetherHandle, fromP, toP);
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Kill(inst->sweHandle);
            if (inst->tetherHandle > 0) VFX_HydroTether_Kill(inst->tetherHandle);
            inst->sweHandle = -1;
            inst->tetherHandle = 0;
            inst->active = false;
        }
    }
}

void DrawTtqpNghichLuuCamNaSkill(void) {
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        NghichLuuInst *inst = &s_inst[i];
        if (!inst->active || inst->impacted) continue;
        float t01 = (inst->maxRange > 0.01f) ? (inst->travelled / inst->maxRange) : 1.0f;
        VFX_ComposeWaterRing(inst->headPos, 1.1f * inst->sizeScale, t01);
    }
}

void UnloadTtqpNghichLuuCamNaSkill(void) {
    for (int i = 0; i < MAX_NLCN_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
