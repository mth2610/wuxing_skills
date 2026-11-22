#include "skills/water/ttqp_pha_lang_quyen_skill/ttqp_pha_lang_quyen_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/liquid/liquid_swe.h"
#include "core/liquid/liquid_impact.h"
#include "core/composition/visual_composer.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PLQ_INST 4

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
} PhaLangInst;

static PhaLangInst s_inst[MAX_PLQ_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpPhaLangQuyen)

static void SpawnWaterSplashSSF(Vector3 pos, Vector3 dir, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = Vector3Normalize((Vector3){ dir.x, 0.45f, dir.z });
    ev.initialVelocity = (Vector3){ dir.x * 6.5f, 3.2f, dir.z * 6.5f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static void AbortTtqpPhaLangQuyen(int agentId) {
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpPhaLangQuyen(int agentId) {
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpPhaLangQuyenSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_PHA_LANG_QUYEN");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_PHA_LANG_QUYEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpPhaLangQuyen);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpPhaLangQuyen);
    }
}

void CastTtqpPhaLangQuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_inst[0].sweHandle >= 0) LiquidSWE_Kill(s_inst[0].sweHandle);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_PHA_LANG_QUYEN");
    float baseDmg = def ? def->baseDamage : 18.5f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float dist = Vector3Length(dir);
    if (dist < 0.01f) dir = (Vector3){ 0.0f, 0.0f, 1.0f };
    else dir = Vector3Scale(dir, 1.0f / dist);
    float range = 7.5f * scale;
    Vector3 endPos = Vector3Add(startPos, Vector3Scale(dir, range));

    Skill_RequestAnim(agentId, "elbow_punch", 0.30f);
    const Agent *owner = Entity_GetAgent(agentId);
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(agentId);
    if (owner && anim) {
        SkillHelper_StartMeleeTrails(agentId, anim, owner->position, atan2f(dir.x, dir.z), 0, 0.30f);
    }

    PhaLangInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->origin = startPos;
    inst->dir = dir;
    inst->headPos = Vector3Add(startPos, Vector3Scale(dir, 0.6f));
    inst->travelled = 0.0f;
    inst->maxRange = range;
    inst->elapsed = 0.0f;
    inst->duration = 0.90f;
    inst->damage = baseDmg;
    inst->sizeScale = scale;
    inst->impacted = false;

    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_TAI_CHI_WHIP;
    desc.source = startPos;
    desc.target = endPos;
    desc.waveHeight = 0.62f * scale;
    desc.waveSpeed = 16.0f;
    desc.width = 4.0f * scale;
    desc.length = range;
    desc.duration = inst->duration;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.0f;
    inst->sweHandle = LiquidSWE_Spawn(&desc);

    SpawnWaterSplashSSF(Vector3Add(startPos, Vector3Scale(dir, 0.5f)), dir, 0.35f, 0.7f * scale);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpPhaLangQuyenSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    /* Process Đấu Pháp clash events for traveling wave projectiles */
    const ClashEvent *events = NULL;
    int evCount = Combat_PeekEvents(&events);
    int baseId = (s_skillIndex >= 0 ? s_skillIndex : 90) * 1000;

    for (int i = 0; i < MAX_PLQ_INST; i++) {
        PhaLangInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        int instId = baseId + i;
        if (!inst->impacted) {
            for (int e = 0; e < evCount; e++) {
                if (events[e].skillInstanceId != instId) continue;
                if (events[e].outcome == CLASH_B_WINS || events[e].outcome == CLASH_MUTUAL_DESTROY) {
                    inst->impacted = true;
                    SpawnWaterSplashSSF(events[e].clashPoint, inst->dir, 0.60f, 0.95f * inst->sizeScale);
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                    break;
                } else if (events[e].outcome == CLASH_HIT_AGENT) {
                    inst->impacted = true;
                    int victim = events[e].otherAgentId;
                    if (victim >= 0) {
                        Entity_AddModifier(victim, 0.80f, 1.0f); /* Slow 20% 1.0s */
                        VoHoc_ApplyWaterMark(inst->ownerAgentId, victim, 1);
                    }
                    SpawnWaterSplashSSF(events[e].clashPoint, inst->dir, 0.70f, 1.05f * inst->sizeScale);
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                    break;
                }
            }
        }

        /* Advance real traveling wave front (16 m/s) so enemy can dodge or clash */
        if (!inst->impacted) {
            float step = 16.0f * dt;
            inst->travelled += step;
            inst->headPos = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->travelled));
            if (inst->travelled >= inst->maxRange) {
                inst->impacted = true;
                SpawnWaterSplashSSF(inst->headPos, inst->dir, 0.45f, 0.85f * inst->sizeScale);
            } else {
                Vector3 submitPos = { inst->headPos.x, inst->headPos.y + 0.8f, inst->headPos.z };
                Combat_SubmitProjectile(inst->ownerAgentId, ELEM_WATER, submitPos,
                                        0.75f * inst->sizeScale, inst->damage, 1.0f, instId);
            }
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Kill(inst->sweHandle);
            inst->sweHandle = -1;
            inst->active = false;
        }
    }
}

void DrawTtqpPhaLangQuyenSkill(void) {
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        PhaLangInst *inst = &s_inst[i];
        if (!inst->active || inst->impacted) continue;
        float t01 = (inst->maxRange > 0.01f) ? (inst->travelled / inst->maxRange) : 1.0f;
        VFX_ComposeWaterRing(inst->headPos, 0.85f * inst->sizeScale, t01);
    }
}

void UnloadTtqpPhaLangQuyenSkill(void) {
    for (int i = 0; i < MAX_PLQ_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
