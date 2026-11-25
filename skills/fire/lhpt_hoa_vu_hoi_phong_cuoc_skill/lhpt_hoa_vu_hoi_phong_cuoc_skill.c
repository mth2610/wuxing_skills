#include "skills/fire/lhpt_hoa_vu_hoi_phong_cuoc_skill/lhpt_hoa_vu_hoi_phong_cuoc_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "core/composition/visual_composer.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_HVHP_INST 4

typedef struct {
    bool  active;
    int   ownerAgentId;
    float elapsed;
    float duration;
    float radius;
    int   deflectsRemaining;
    int   wardHandle;
} HoaVuInst;

static HoaVuInst s_inst[MAX_HVHP_INST];
static int       s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(LhptHoaVuHoiPhongCuoc)

/* Airborne Shader-Puff wisps & foot sweep fire (no trails, no VolumeTrail, no decals) */
static void EmitAirborneWispPuff(Vector3 pos, float radius, float life, int count, float intensity)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne guard wisp: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = radius;
    cfg.sourceRadius    = radius * 0.45f;
    cfg.buoyancy        = 2.0f;
    cfg.turbulenceSpeed = 1.8f;
    cfg.distortion      = 0.52f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.45f;
    cfg.intensity       = intensity;
    cfg.emissiveBoost   = 1.55f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = false;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void AbortLhptHoaVu(int agentId)
{
    for (int i = 0; i < MAX_HVHP_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].wardHandle >= 0) VFX_FirePhoenixWard_Stop(s_inst[i].wardHandle);
            s_inst[i].wardHandle = -1;
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptHoaVu(int agentId)
{
    for (int i = 0; i < MAX_HVHP_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptHoaVuHoiPhongCuocSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_HVHP_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_HOA_VU_HOI_PHONG_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_HOA_VU_HOI_PHONG_CUOC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptHoaVu);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptHoaVu);
    }
}

void CastLhptHoaVuHoiPhongCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    (void)startPos;
    int slot = -1;
    for (int i = 0; i < MAX_HVHP_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    float scale     = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult   = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float shieldAmt = 20.0f * pveMult;

    /* Khởi & Phát: Đá quét thấp 360° (standing_melee_attack_360_low) kiểu Zuko -> Giải CC, Khiên 20, +20 Mana, +35% tốc */
    Skill_RequestAnim(agentId, "standing_melee_attack_360_low", 0.55f);
    Entity_Cleanse(agentId);
    Entity_AddShield(agentId, shieldAmt, 4.0f);
    Entity_RestoreMana(agentId, 20.0f);
    Entity_AddModifier(agentId, 1.35f, 3.5f);

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;

    HoaVuInst *inst = &s_inst[slot];
    if (inst->active && inst->wardHandle >= 0) VFX_FirePhoenixWard_Stop(inst->wardHandle);
    *inst = (HoaVuInst){0};
    inst->active            = true;
    inst->ownerAgentId      = agentId;
    inst->elapsed           = 0.0f;
    inst->duration          = 4.0f;
    inst->radius            = 1.9f * scale;
    inst->deflectsRemaining = 2;
    inst->wardHandle        = VFX_FirePhoenixWard_Spawn(anchor, 1.05f * scale, 2, 4.0f);

    VFX_ComposeCleanseBurst(anchor, VC_MAT_FIRE, 1.45f * scale);
    VFX_FireKickArc_Spawn((Vector3){ anchor.x, anchor.y + 0.52f, anchor.z },
                          (Vector3){ 0.0f, 0.0f, 1.0f }, (Vector3){ 0.0f, 1.0f, 0.0f },
                          1.65f * scale, 340.0f, 0.36f, scale);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateLhptHoaVuHoiPhongCuocSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_HVHP_INST; i++) {
        HoaVuInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner || !owner->active || inst->elapsed >= inst->duration) {
            if (inst->wardHandle >= 0) VFX_FirePhoenixWard_Stop(inst->wardHandle);
            inst->wardHandle = -1;
            inst->active = false;
            continue;
        }
        if (inst->wardHandle >= 0) {
            VFX_FirePhoenixWard_SetCenter(inst->wardHandle, owner->position);
        }

        const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
        if (inst->elapsed <= 0.55f && anim) {
            /* Every-frame Shader-Puff fire whipping off the 360° low-sweeping RightFoot */
            Vector3 footPos;
            if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                            "mixamorig:RightFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &footPos)) {
                EmitAirborneWispPuff(footPos, 0.24f, 0.28f, 65, 2.30f);
            }
        }

        /* Thermal deflection: incinerate up to 2 enemy projectiles at exact intercept point */
        if (inst->deflectsRemaining > 0) {
            CombatDeflectInfo defs[4];
            int broken = Combat_DeflectProjectilesInRadiusEx(owner->position, inst->radius,
                                                             inst->ownerAgentId, inst->deflectsRemaining, defs, 4);
            if (broken > 0) {
                for (int d = 0; d < broken && inst->deflectsRemaining > 0; d++) {
                    inst->deflectsRemaining--;
                    Vector3 hitPos = defs[d].pos;
                    Vector3 nrm = Vector3Subtract(hitPos, owner->position);
                    nrm.y = 0.0f;
                    nrm = (Vector3Length(nrm) > 0.01f) ? Vector3Normalize(nrm) : (Vector3){ 0.0f, 1.0f, 0.0f };
                    if (inst->wardHandle >= 0) VFX_FirePhoenixWard_ExpendFeather(inst->wardHandle, hitPos);
                    VFX_ComposeDeflectFlash(hitPos, nrm, VC_MAT_FIRE, 0.90f);
                }
                PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
            }
        }
    }
}

void DrawLhptHoaVuHoiPhongCuocSkill(void) { }

void UnloadLhptHoaVuHoiPhongCuocSkill(void)
{
    for (int i = 0; i < MAX_HVHP_INST; i++) {
        if (s_inst[i].active && s_inst[i].wardHandle >= 0) VFX_FirePhoenixWard_Stop(s_inst[i].wardHandle);
        s_inst[i].wardHandle = -1;
        s_inst[i].active = false;
    }
}
