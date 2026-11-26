#include "skills/water/ttqp_trieu_tich_linh_tuyen_skill/ttqp_trieu_tich_linh_tuyen_skill.h"
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

#define MAX_TTLT_INST 4

typedef struct {
    bool      active;
    int       ownerAgentId;
    AgentTeam allyTeam;
    Vector3   center;
    float     elapsed;
    float     duration;
    float     tickAccum;
    float     markAccum;
    float     healPerTick;
    float     sizeScale;
    int       sweHandle;
} TrieuTichInst;

static TrieuTichInst s_inst[MAX_TTLT_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpTrieuTichLinhTuyen)

static void SpawnSpringPulseSSF(Vector3 pos, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.initialVelocity = (Vector3){ 0.0f, 3.8f, 0.0f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static void AbortTtqpTrieuTich(int agentId) {
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpTrieuTich(int agentId) {
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpTrieuTichLinhTuyenSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_TRIEU_TICH_LINH_TUYEN");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_TRIEU_TICH_LINH_TUYEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpTrieuTich);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpTrieuTich);
    }
}

void CastTtqpTrieuTichLinhTuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active && s_inst[slot].sweHandle >= 0) {
        LiquidSWE_Kill(s_inst[slot].sweHandle);
    }

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);

    /* Self-centered radial spring at caster's feet (compliant with §2.1) */
    Skill_RequestAnim(agentId, "hell_slammer_a", 0.40f);
    TrieuTichInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->allyTeam = owner ? owner->team : TEAM_ALLY;
    inst->center = owner ? owner->position : startPos;
    inst->elapsed = 0.0f;
    inst->duration = 5.0f;
    inst->tickAccum = 0.5f;
    inst->markAccum = 0.0f;
    inst->healPerTick = 3.0f * pveMult;
    inst->sizeScale = scale;

    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_RADIAL_DELUGE;
    desc.source = inst->center;
    desc.target = inst->center;
    desc.waveHeight = 0.50f * scale;
    desc.waveSpeed = 6.5f;
    desc.width = 8.0f * scale;
    desc.length = 8.0f * scale;
    desc.duration = inst->duration;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.0f;
    inst->sweHandle = LiquidSWE_Spawn(&desc);

    SpawnSpringPulseSSF(inst->center, 0.75f, 1.25f * scale);
VFX_ComposeMistVeilEx(inst->center, VC_MAT_WATER, 4.0f * scale, inst->duration);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpTrieuTichLinhTuyenSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        TrieuTichInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        inst->tickAccum += dt;
        inst->markAccum += dt;

        float radius = 4.0f * inst->sizeScale;

        if (inst->tickAccum >= 0.50f) {
            inst->tickAccum -= 0.50f;
            int ids[24];
            int cnt = Entity_GetNearbyTargets(inst->center, radius, ids, 24);
            for (int k = 0; k < cnt; k++) {
                const Agent *a = Entity_GetAgent(ids[k]);
                if (!a) continue;
                if (a->team == inst->allyTeam) {
                    Entity_Heal(ids[k], inst->healPerTick);
                    Entity_RestoreMana(ids[k], 4.0f);
                    SkillManager_ReduceAgentCooldowns(ids[k], 0.20f);
                } else if (Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) {
                    Entity_AddModifier(ids[k], 0.70f, 0.75f);
                }
            }
        }

        if (inst->markAccum >= 2.50f) {
            inst->markAccum -= 2.50f;
            SpawnSpringPulseSSF(inst->center, 0.55f, 1.10f * inst->sizeScale);
            int ids[24];
            int cnt = Entity_GetNearbyTargets(inst->center, radius, ids, 24);
            for (int k = 0; k < cnt; k++) {
                if (Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) {
                    VoHoc_ApplyWaterMark(inst->ownerAgentId, ids[k], 1);
                }
            }
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Stop(inst->sweHandle);
            inst->sweHandle = -1;
            inst->active = false;
        }
    }
}

void DrawTtqpTrieuTichLinhTuyenSkill(void) {
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        TrieuTichInst *inst = &s_inst[i];
        if (!inst->active) continue;
        float t01 = inst->elapsed / inst->duration;
        float radius = 4.0f * inst->sizeScale;
        VFX_ComposeRuneCircle(inst->center, (Vector3){ 0.0f, 1.0f, 0.0f },
                              VC_MAT_WATER, radius, t01, 2);
        VFX_ComposeWaterRing(inst->center, radius * 0.95f, fmodf(inst->elapsed * 0.65f, 1.0f));
    }
}

void UnloadTtqpTrieuTichLinhTuyenSkill(void) {
    for (int i = 0; i < MAX_TTLT_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
