#include "skills/fire/lhpt_phi_hoa_xung_cuoc_skill/lhpt_phi_hoa_xung_cuoc_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PHXC_INST   4
#define PHXC_HIT_RADIUS 1.65f

typedef struct {
    bool    active;
    bool    dashed;
    bool    struck;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   castSecs;
    float   damage;
    float   sizeScale;
} PhiHoaInst;

static PhiHoaInst s_inst[MAX_PHXC_INST];
static int        s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(LhptPhiHoaXungCuoc)

/* Every-frame airborne Shader-Puff thruster plume (no trails, no VolumeTrail) */
static void EmitFootThrusterPuff(Vector3 pos, float radius, float life, int count, bool dynLight)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FLAMETHROWER);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne foot thruster: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = radius;
    cfg.sourceRadius    = radius * 0.55f;
    cfg.buoyancy        = 2.8f;
    cfg.turbulenceSpeed = 2.3f;
    cfg.distortion      = 0.56f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.40f;
    cfg.intensity       = 1.80f;
    cfg.emissiveBoost   = 1.40f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = dynLight;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void EmitImpactBlastPuff(Vector3 pos, float scale)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF;
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_BURST;
    cfg.motionType      = VFX_FIRE_MOTION_BLAST;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = 85;
    cfg.particleRadius  = 0.44f * scale;
    cfg.sourceRadius    = 0.48f * scale;
    cfg.fieldSpeed      = 6.8f;
    cfg.buoyancy        = 3.4f;
    cfg.turbulenceSpeed = 2.5f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = 0.45f;
    cfg.coreHeat        = 1.45f;
    cfg.intensity       = 1.95f;
    cfg.emissiveBoost   = 1.45f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = true;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void AbortLhptPhiHoa(int agentId)
{
    for (int i = 0; i < MAX_PHXC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptPhiHoa(int agentId)
{
    for (int i = 0; i < MAX_PHXC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptPhiHoaXungCuocSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_PHXC_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_PHI_HOA_XUNG_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHI_HOA_XUNG_CUOC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptPhiHoa);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptPhiHoa);
    }
}

void CastLhptPhiHoaXungCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_PHXC_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHI_HOA_XUNG_CUOC");
    float baseDmg = def ? def->baseDamage : 21.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Bật đà phun lửa phản lực từ chân trụ, tung Phi Hỏa Xung Cước (flying_kick) */
    Skill_RequestAnim(agentId, "flying_kick", 0.54f);
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(agentId);

    Vector3 footPos = { anchor.x, anchor.y + 0.55f, anchor.z };
    if (owner && anim) {
        SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                    "mixamorig:LeftFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &footPos);
    }

    PhiHoaInst *inst = &s_inst[slot];
    *inst = (PhiHoaInst){0};
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->dir          = dir;
    inst->castSecs     = def ? def->castSecs : 0.20f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    EmitFootThrusterPuff(footPos, 0.28f * scale, 0.30f, 95, true);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

static void TriggerPhiHoaImpact(PhiHoaInst *inst, Vector3 impactPos)
{
    inst->struck = true;

    EmitImpactBlastPuff(impactPos, inst->sizeScale);
    VFX_ComposeFireballBurst(impactPos, VC_MAT_FIRE, 0.95f * inst->sizeScale, 0.88f);
    VFX_MartialShockCone_Spawn(impactPos, inst->dir, VC_MAT_FIRE,
                               2.4f * inst->sizeScale, 1.05f * inst->sizeScale, 0.28f);
    VFX_SpawnGroundWave((Vector3){ impactPos.x, MapManager_GetGroundHeightAt(impactPos.x, impactPos.z) + 0.05f, impactPos.z },
                        VC_MAT_FIRE, PHXC_HIT_RADIUS * inst->sizeScale * 1.15f, 0.32f);
    VFX_ComposeEmberBurst(impactPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 1.15f * inst->sizeScale, 0.90f);
    Wind_SpawnRadialBlast(impactPos, 2.4f * inst->sizeScale, 7.0f, 0.18f);

    int targets[12];
    int cnt = Entity_GetNearbyTargets(impactPos, PHXC_HIT_RADIUS * inst->sizeScale, targets, 12);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, Vector3Scale(inst->dir, 1.6f));
        Entity_ApplyStatus(tid, STATUS_BURN, 10.0f / 3.0f, 3.0f, inst->ownerAgentId);
        Entity_ApplyStatus(tid, STATUS_VULNERABLE, 0.20f, 3.0f, inst->ownerAgentId);
        VoHoc_ApplyFireMark(inst->ownerAgentId, tid, 1);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.045f, 0.08f);
        PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
    }
}

void UpdateLhptPhiHoaXungCuocSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_PHXC_INST; i++) {
        PhiHoaInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);

        Vector3 rightFoot = owner ? Vector3Add(owner->position, Vector3Scale(inst->dir, 0.85f)) : inst->origin;
        rightFoot.y = (owner ? owner->position.y : inst->origin.y) + 0.90f;
        Vector3 leftFoot = owner ? Vector3Subtract(owner->position, Vector3Scale(inst->dir, 0.35f)) : rightFoot;
        leftFoot.y = rightFoot.y - 0.15f;

        if (owner && anim) {
            SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                        "mixamorig:RightFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &rightFoot);
            SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                        "mixamorig:LeftFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &leftFoot);
        }

        /* Phát: Phóng lướt bằng lực đẩy phản lực ở chân (F = m_dot * v_e) */
        if (!inst->dashed && inst->elapsed >= inst->castSecs * 0.55f) {
            inst->dashed = true;
            Entity_Dash(inst->ownerAgentId, inst->dir, 19.0f);
            Vector3 dashOrigin = owner ? owner->position : inst->origin;
            dashOrigin.y += 0.95f;
            VFX_MartialShockCone_Spawn(dashOrigin, inst->dir, VC_MAT_FIRE,
                                       2.2f * inst->sizeScale, 0.78f * inst->sizeScale, 0.26f);
        }

        if (!inst->struck) {
            /* Every-frame billowing Shader-Puff exhaust from trailing heel + blazing striking foot */
            EmitFootThrusterPuff(leftFoot, 0.28f * inst->sizeScale, 0.30f, 95, true);
            EmitFootThrusterPuff(rightFoot, 0.24f * inst->sizeScale, 0.24f, 75, false);
        }

        if (inst->dashed && !inst->struck) {
            int nearIds[8];
            int nearCnt = Entity_GetNearbyTargets(rightFoot, 1.15f * inst->sizeScale, nearIds, 8);
            bool contact = false;
            for (int k = 0; k < nearCnt; k++) {
                if (nearIds[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, nearIds[k])) {
                    contact = true;
                    break;
                }
            }
            if (contact || inst->elapsed >= inst->castSecs + 0.22f) {
                TriggerPhiHoaImpact(inst, rightFoot);
            }
        }

        if (inst->elapsed >= inst->castSecs + 0.36f) {
            inst->active = false;
        }
    }
}

void DrawLhptPhiHoaXungCuocSkill(void) { }

void UnloadLhptPhiHoaXungCuocSkill(void)
{
    for (int i = 0; i < MAX_PHXC_INST; i++) s_inst[i].active = false;
}
