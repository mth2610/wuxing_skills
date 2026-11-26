#include "skills/water/ttqp_thuy_kinh_quyen_thuan_skill/ttqp_thuy_kinh_quyen_thuan_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
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

#define MAX_TKQT_INST 4

typedef struct {
    bool  active;
    int   ownerAgentId;
    float elapsed;
    float duration;
    int   deflectsRemaining;
    int   sweHandle;
    int   shieldHandle;
    int   mirrorHandle;
} ThuyKinhInst;

static ThuyKinhInst s_inst[MAX_TKQT_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpThuyKinhQuyenThuan)

static void SpawnDeflectSplashSSF(Vector3 pos, float force01) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.initialVelocity = (Vector3){ 0.0f, 4.5f, 0.0f };
    ev.force01 = force01;
    ev.scale = 1.0f;
    LiquidImpact_SpawnWater(&ev);
}

static void KillThuyKinhHandles(ThuyKinhInst *inst) {
    if (inst->sweHandle >= 0) { LiquidSWE_Stop(inst->sweHandle); inst->sweHandle = -1; }
    if (inst->shieldHandle >= 0) { VFX_FlowShield_Stop(inst->shieldHandle); inst->shieldHandle = -1; }
    if (inst->mirrorHandle > 0) { VFX_WaterMirror_Stop(inst->mirrorHandle); inst->mirrorHandle = 0; }
}

static void AbortTtqpThuyKinh(int agentId) {
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillThuyKinhHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTtqpThuyKinh(int agentId) {
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpThuyKinhQuyenThuanSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
        s_inst[i].shieldHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_THUY_KINH_QUYEN_THUAN");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_THUY_KINH_QUYEN_THUAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpThuyKinh);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpThuyKinh);
    }
}

void CastTtqpThuyKinhQuyenThuanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillThuyKinhHandles(&s_inst[slot]);

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float shieldAmt = 34.0f * pveMult;

    /* Immediate cleanse + self shield + speed haste */
    Skill_RequestAnim(agentId, "taiji_cloud_hands", 0.38f);
    Entity_Cleanse(agentId);
    Entity_AddShield(agentId, shieldAmt, 4.0f);
    Entity_AddModifier(agentId, 1.20f, 3.0f);
    StatusVFX_Attach(agentId, EFFECT_PRESET_WATER_SPLASH, 4.0f);

    /* Nearby allies within 5.0m receive 50% shield + speed */
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    if (owner) {
        int allies[16];
        int cnt = Entity_GetNearbyTargetsTeam(owner->position, 5.0f * scale, owner->team, allies, 16);
        for (int k = 0; k < cnt; k++) {
            if (allies[k] == agentId) continue;
            Entity_AddShield(allies[k], shieldAmt * 0.50f, 4.0f);
            Entity_AddModifier(allies[k], 1.20f, 3.0f);
        }
    }

    ThuyKinhInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->elapsed = 0.0f;
    inst->duration = 4.0f;
    inst->deflectsRemaining = 2;

    /* 3D flowing water shield dome grounded at anchor (FlowShield adds +0.5*r internally) */
    inst->shieldHandle = VFX_FlowShield_Spawn(anchor, VC_MAT_WATER, 1.45f * scale, 1.0f);
    inst->mirrorHandle = VFX_WaterMirror_Spawn(anchor, 1.15f * scale, 2, 4.0f);
    VFX_ComposeCleanseBurst(anchor, VC_MAT_WATER, 1.55f * scale);
LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_ORBITAL_CHAKRAM;
    desc.source = anchor;
    desc.target = anchor;
    desc.waveHeight = 0.55f * scale;
    desc.waveSpeed = 9.0f;
    desc.width = 4.5f * scale;
    desc.length = 4.5f * scale;
    desc.duration = inst->duration;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.15f;
    desc.fastMartialStrike = true;
    inst->sweHandle = LiquidSWE_Spawn(&desc);

    SpawnDeflectSplashSSF(anchor, 0.50f);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpThuyKinhQuyenThuanSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        ThuyKinhInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (owner) {
            if (inst->shieldHandle >= 0) {
                VFX_FlowShield_SetTransform(inst->shieldHandle, owner->position);
                float remainFrac = 1.0f - (inst->elapsed / inst->duration);
                VFX_FlowShield_SetIntensity(inst->shieldHandle, remainFrac > 0.2f ? 1.0f : remainFrac * 5.0f);
            }
            if (inst->mirrorHandle > 0) {
                VFX_WaterMirror_SetCenter(inst->mirrorHandle, owner->position);
            }
            if (inst->sweHandle >= 0) {
                LiquidSWE_SetOrigin(inst->sweHandle, owner->position, (Vector3){ 0.0f, 0.0f, 1.0f });
            }
            if (inst->deflectsRemaining > 0) {
                CombatDeflectInfo dHits[2];
                int deflected = Combat_DeflectProjectilesInRadiusEx(owner->position, 1.85f, inst->ownerAgentId,
                                                                    inst->deflectsRemaining, dHits, 2);
                if (deflected > 0) {
                    inst->deflectsRemaining -= deflected;
                    if (inst->deflectsRemaining < 0) inst->deflectsRemaining = 0;
                    for (int b = 0; b < deflected; b++) {
                        VFX_WaterMirror_ShatterOne(inst->mirrorHandle, dHits[b].pos);
                        SpawnDeflectSplashSSF(dHits[b].pos, 0.85f);
                    }
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                }
            }
        }

        if (!owner || inst->elapsed >= inst->duration) {
            KillThuyKinhHandles(inst);
            inst->active = false;
        }
    }
}

void DrawTtqpThuyKinhQuyenThuanSkill(void) {}

void UnloadTtqpThuyKinhQuyenThuanSkill(void) {
    for (int i = 0; i < MAX_TKQT_INST; i++) {
        KillThuyKinhHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
