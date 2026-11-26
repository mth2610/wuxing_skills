#include "skills/water/ttqp_no_hai_long_quyen_skill/ttqp_no_hai_long_quyen_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/liquid/liquid_swe.h"
#include "core/liquid/liquid_impact.h"
#include "core/composition/visual_composer.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_NHLQ_INST 4

typedef struct {
    bool    active;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   duration;
    float   damage;
    float   sizeScale;
    bool    hitApplied;
    bool    shattered;
    int     sweHandle;
} NoHaiInst;

static NoHaiInst s_inst[MAX_NHLQ_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpNoHaiLongQuyen)

static void SpawnPunchSplashSSF(Vector3 pos, Vector3 dir, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = Vector3Normalize((Vector3){ dir.x, 0.55f, dir.z });
    ev.initialVelocity = (Vector3){ dir.x * 9.0f, 4.5f, dir.z * 9.0f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static void AbortTtqpNoHai(int agentId) {
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpNoHai(int agentId) {
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpNoHaiLongQuyenSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_NO_HAI_LONG_QUYEN");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_NO_HAI_LONG_QUYEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpNoHai);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpNoHai);
    }
}

void CastTtqpNoHaiLongQuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_inst[0].sweHandle >= 0) LiquidSWE_Kill(s_inst[0].sweHandle);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_NO_HAI_LONG_QUYEN");
    float baseDmg = def ? def->baseDamage : 38.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float len = Vector3Length(dir);
    if (len < 0.01f) dir = (Vector3){ 0.0f, 0.0f, 1.0f };
    else dir = Vector3Scale(dir, 1.0f / len);

    /* Forward melee lunge from caster */
    Entity_Dash(agentId, dir, 18.0f);
    Skill_RequestAnim(agentId, "flying_kick", 0.32f);
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(agentId);
    if (anim) {
        SkillHelper_StartMeleeTrails(agentId, anim, startPos, atan2f(dir.x, dir.z), 0, 0.38f);
    }

    Vector3 impactPos = Vector3Add(startPos, Vector3Scale(dir, 2.8f * scale));
    NoHaiInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->origin = impactPos;
    inst->dir = dir;
    inst->elapsed = 0.0f;
    inst->duration = 1.30f;
    inst->damage = baseDmg;
    inst->sizeScale = scale;
    inst->hitApplied = false;
    inst->shattered = false;

    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_TWIN_TIDAL;
    desc.source = startPos;
    desc.target = impactPos;
    desc.waveHeight = 1.15f * scale;
    desc.waveSpeed = 13.5f;
    desc.width = 5.0f * scale;
    desc.length = 5.0f * scale;
    desc.duration = inst->duration;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.0f;
    desc.fastMartialStrike = true;
    desc.allowCompactBounds = true;
    inst->sweHandle = LiquidSWE_Spawn(&desc);

    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpNoHaiLongQuyenSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        NoHaiInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        if (!inst->hitApplied && inst->elapsed >= 0.16f) {
            inst->hitApplied = true;
            Skill_RequestAnim(inst->ownerAgentId, "fist_fight_a", 0.35f);
            Vector3 punchCenter = inst->origin;
            const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
            if (owner) {
                punchCenter = Vector3Add(owner->position, Vector3Scale(inst->dir, 1.3f));
                inst->origin = punchCenter;
            }
            /* Heavy SSF hydro-punch explosion + directional shock-cone at impact point */
            SpawnPunchSplashSSF(punchCenter, inst->dir, 1.0f, 1.35f * inst->sizeScale);
            VFX_MartialShockCone_Spawn((Vector3){ punchCenter.x, punchCenter.y + 0.95f, punchCenter.z },
                                       inst->dir, VC_MAT_WATER, 2.5f * inst->sizeScale, 0.95f * inst->sizeScale, 0.30f);
int ids[16];
            int cnt = Entity_GetNearbyTargets(punchCenter, 1.8f * inst->sizeScale, ids, 16);
            bool anyHit = false;
            for (int k = 0; k < cnt; k++) {
                if (!Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) continue;
                float mult = 1.0f;
                float shatterBonus = VoHoc_OnNoHaiConsume(inst->ownerAgentId, ids[k], s_skillIndex, &mult);
                if (shatterBonus > 0.0f) inst->shattered = true;
                float totalDmg = inst->damage * mult + shatterBonus;
                Vector3 kb = Vector3Scale(inst->dir, 3.5f);
                Entity_ApplyDamageFrom(ids[k], inst->ownerAgentId, totalDmg, 0.0f, kb);
                Entity_ApplyStun(ids[k], 0.60f);
                anyHit = true;
            }
            if (anyHit) PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Kill(inst->sweHandle);
            inst->sweHandle = -1;
            inst->active = false;
        }
    }
}

void DrawTtqpNoHaiLongQuyenSkill(void) {
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        NoHaiInst *inst = &s_inst[i];
        if (!inst->active || !inst->hitApplied) continue;
        float post = (inst->elapsed - 0.16f) / 0.55f;
        if (post >= 0.0f && post < 1.0f) {
            VFX_ComposeWaterRing(inst->origin, 2.2f * inst->sizeScale, post);
            if (inst->shattered) {
                VFX_DrawIceCrystalBurst(inst->origin, 6, 99 + i * 7, post);
            }
        }
    }
}

void UnloadTtqpNoHaiLongQuyenSkill(void) {
    for (int i = 0; i < MAX_NHLQ_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
