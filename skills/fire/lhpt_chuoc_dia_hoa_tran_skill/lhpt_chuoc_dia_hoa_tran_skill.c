#include "skills/fire/lhpt_chuoc_dia_hoa_tran_skill/lhpt_chuoc_dia_hoa_tran_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CDHT_INST 4
#define CDHT_RADIUS   3.8f
#define CDHT_DURATION 5.0f

typedef struct {
    bool      active;
    int       ownerAgentId;
    AgentTeam allyTeam;
    Vector3   center;
    float     elapsed;
    float     tickAccum;
    float     markAccum;
    float     sizeScale;
    int       formHandle;
} ChuocDiaInst;

static ChuocDiaInst s_inst[MAX_CDHT_INST];
static int          s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(LhptChuocDiaHoaTran)

/* Every-frame ground-clinging persistent fire ring (C11: y <= 0.25m, Shader-Tongue ONLY on terrain) */
static void EmitGroundRingTongue(Vector3 groundCenter, float radius, float life, int count)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_GROUND_FIRE);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_TONGUE;    /* Ground-hugging fire -> Shader-Tongue */
    cfg.emitterShape    = VFX_FIRE_EMITTER_RING;
    cfg.emissionMode    = VFX_FIRE_EMIT_PULSING;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_SURFACE_CLING;  /* Clings to terrain surface */
    cfg.particleCount   = count;
    cfg.particleRadius  = 0.22f;                          /* Low profile (y <= 0.25m, C11) */
    cfg.sourceRadius    = radius;
    cfg.buoyancy        = 0.65f;
    cfg.turbulenceSpeed = 1.4f;
    cfg.distortion      = 0.52f;
    cfg.softness        = 0.35f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.35f;
    cfg.intensity       = 1.70f;                          /* Restrained Tier 1 support ground layer */
    cfg.emissiveBoost   = 1.35f;
    cfg.emitSparks      = false;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = false;
    VFX_ComposeGuidedFire(groundCenter, &cfg);
}

static void AbortLhptChuocDia(int agentId)
{
    for (int i = 0; i < MAX_CDHT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].formHandle >= 0) VFX_FireScorchFormation_Stop(s_inst[i].formHandle);
            s_inst[i].formHandle = -1;
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptChuocDia(int agentId)
{
    for (int i = 0; i < MAX_CDHT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptChuocDiaHoaTranSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_CDHT_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_CHUOC_DIA_HOA_TRAN");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_CHUOC_DIA_HOA_TRAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptChuocDia);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptChuocDia);
    }
}

void CastLhptChuocDiaHoaTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_CDHT_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 center = owner ? owner->position : startPos;
    center.y = MapManager_GetGroundHeightAt(center.x, center.z) + 0.04f;

    /* Khởi & Phát: Dậm gót mạnh xuống đất (mma_kick) trải vòng lửa bám địa hình (Shader-Tongue) */
    Skill_RequestAnim(agentId, "mma_kick", 0.44f);

    ChuocDiaInst *inst = &s_inst[slot];
    if (inst->active && inst->formHandle >= 0) VFX_FireScorchFormation_Stop(inst->formHandle);
    *inst = (ChuocDiaInst){0};
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->allyTeam     = owner ? owner->team : TEAM_ALLY;
    inst->center       = center;
    inst->tickAccum    = 0.50f;
    inst->markAccum    = 0.50f;
    inst->sizeScale    = scale;

    float r = CDHT_RADIUS * scale;
    inst->formHandle   = VFX_FireScorchFormation_Spawn(center, r, CDHT_DURATION);
EmitGroundRingTongue(center, r * 0.92f, 0.50f, 90);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateLhptChuocDiaHoaTranSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_CDHT_INST; i++) {
        ChuocDiaInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed   += dt;
        inst->tickAccum += dt;
        inst->markAccum += dt;
        float r = CDHT_RADIUS * inst->sizeScale;

        /* Smooth every-frame ground-hugging Shader-Tongue perimeter ring (C11 ground layer) */
        EmitGroundRingTongue(inst->center, r * 0.92f, 0.36f, 90);

        if (inst->tickAccum >= 0.50f) {
            inst->tickAccum -= 0.50f;
            if (inst->formHandle >= 0) VFX_FireScorchFormation_SetPulse(inst->formHandle, 1.0f);

            int allies[16];
            int allyCnt = Entity_GetNearbyTargetsTeam(inst->center, r, inst->allyTeam, allies, 16);
            for (int k = 0; k < allyCnt; k++) {
                Entity_RestoreMana(allies[k], 3.5f);                 /* 7 Mana/s */
                Entity_AddModifier(allies[k], 1.25f, 0.75f);         /* +25% Haste */
                SkillManager_ReduceAgentCooldowns(allies[k], 0.25f); /* 2.5s CDR over 5s */
            }

            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    Entity_ApplyStatus(enemies[k], STATUS_VULNERABLE, 0.20f, 0.75f, inst->ownerAgentId);
                }
            }
        }

        if (inst->markAccum >= 2.50f) {
            inst->markAccum -= 2.50f;
            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    VoHoc_ApplyFireMark(inst->ownerAgentId, enemies[k], 1);
                }
            }
        }

        if (inst->elapsed >= CDHT_DURATION) {
            if (inst->formHandle >= 0) VFX_FireScorchFormation_Stop(inst->formHandle);
            inst->formHandle = -1;
            inst->active = false;
        }
    }
}

void DrawLhptChuocDiaHoaTranSkill(void) { }

void UnloadLhptChuocDiaHoaTranSkill(void)
{
    for (int i = 0; i < MAX_CDHT_INST; i++) {
        if (s_inst[i].active && s_inst[i].formHandle >= 0) VFX_FireScorchFormation_Stop(s_inst[i].formHandle);
        s_inst[i].formHandle = -1;
        s_inst[i].active = false;
    }
}
