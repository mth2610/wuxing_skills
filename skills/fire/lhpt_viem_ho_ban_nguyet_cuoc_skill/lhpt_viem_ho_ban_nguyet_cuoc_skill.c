#include "skills/fire/lhpt_viem_ho_ban_nguyet_cuoc_skill/lhpt_viem_ho_ban_nguyet_cuoc_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_VHBN_INST   4
#define MAX_VHBN_HIT    16
#define VHBN_MAX_RANGE  5.6f
#define VHBN_SWEEP_SECS 0.38f
#define VHBN_HALF_CONE  0.96f /* ~55 deg half-angle => 110 deg crescent */

typedef struct {
    bool    active;
    bool    sweeping;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 pivot;
    Vector3 dir;
    Vector3 planeNormal;
    float   elapsed;
    float   castSecs;
    float   sweepTimer;
    float   emberAccum;
    float   damage;
    float   sizeScale;
    int     hitIds[MAX_VHBN_HIT];
    int     hitCount;
} ViemHoInst;

static ViemHoInst s_inst[MAX_VHBN_INST];
static int        s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(LhptViemHoBanNguyetCuoc)

static bool AlreadyHit(const ViemHoInst *inst, int tid)
{
    for (int i = 0; i < inst->hitCount; i++) {
        if (inst->hitIds[i] == tid) return true;
    }
    return false;
}

/* Pure every-frame airborne Shader-Puff crescent wave (no sword-slash blade sheet) */
static void EmitCrescentPuff(Vector3 pos, float radius, float life, int count, bool dynLight)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne crescent wave: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = radius;
    cfg.sourceRadius    = radius * 0.55f;
    cfg.buoyancy        = 3.2f;
    cfg.turbulenceSpeed = 2.4f;
    cfg.distortion      = 0.60f;
    cfg.softness        = 0.38f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.35f;
    cfg.intensity       = 1.85f;
    cfg.emissiveBoost   = 1.40f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = dynLight;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void AbortLhptViemHo(int agentId)
{
    for (int i = 0; i < MAX_VHBN_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptViemHo(int agentId)
{
    for (int i = 0; i < MAX_VHBN_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptViemHoBanNguyetCuocSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_VHBN_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_VIEM_HO_BAN_NGUYET_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_VIEM_HO_BAN_NGUYET_CUOC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptViemHo);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptViemHo);
    }
}

void CastLhptViemHoBanNguyetCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_VHBN_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_VIEM_HO_BAN_NGUYET_CUOC");
    float baseDmg = def ? def->baseDamage : 16.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Xoay hông vung cước bán nguyệt (inside_crescent_kick) */
    Skill_RequestAnim(agentId, "inside_crescent_kick", 0.62f);

    ViemHoInst *inst = &s_inst[slot];
    *inst = (ViemHoInst){0};
    inst->active       = true;
    inst->sweeping     = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->pivot        = (Vector3){ anchor.x, MapManager_GetGroundHeightAt(anchor.x, anchor.z) + 1.05f, anchor.z };
    inst->dir          = dir;
    inst->planeNormal  = (Vector3){ 0.0f, 1.0f, 0.0f };
    inst->castSecs     = def ? def->castSecs : 0.28f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateLhptViemHoBanNguyetCuocSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_VHBN_INST; i++) {
        ViemHoInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);

        if (!inst->sweeping) {
            /* Wrap chambering RightFoot in rolling Shader-Puff fire */
            if (owner && anim) {
                Vector3 footPos;
                if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                                "mixamorig:RightFoot", (Vector3){ 0.0f, 0.06f, 0.0f }, &footPos)) {
                    EmitCrescentPuff(footPos, 0.24f * inst->sizeScale, 0.22f, 55, false);
                }
            }
            if (inst->elapsed >= inst->castSecs) {
                /* Phát: Lấy mặt phẳng vung chân trực tiếp từ xương mixamorig:RightFoot */
                inst->sweeping   = true;
                inst->sweepTimer = 0.0f;
                if (owner) {
                    inst->origin = owner->position;
                    inst->pivot  = (Vector3){
                        owner->position.x,
                        MapManager_GetGroundHeightAt(owner->position.x, owner->position.z) + 1.05f,
                        owner->position.z
                    };
                }
                if (owner && anim) {
                    Vector3 bPivot, bDir, bNormal;
                    if (SkillHelper_SampleBoneSwingPlane(anim, owner->position, owner->facingYaw,
                                                         "mixamorig:RightFoot", (Vector3){ 0.0f, 0.15f, 0.25f },
                                                         &bPivot, &bDir, &bNormal)) {
                        inst->pivot = bPivot;
                        if (bNormal.y < 0.0f) bNormal = Vector3Negate(bNormal);
                        inst->planeNormal = Vector3Normalize(Vector3Lerp((Vector3){ 0.0f, 1.0f, 0.0f }, bNormal, 0.45f));
                    }
                }
                Wind_SpawnGust(inst->pivot, inst->dir, 3.0f * inst->sizeScale, 8.5f, 0.22f);
                VFX_FireKickArc_Spawn(inst->pivot, inst->dir, inst->planeNormal,
                                      1.40f * inst->sizeScale, 130.0f, 0.24f, inst->sizeScale);
                VFX_FireCrescentWave_Spawn(inst->origin, inst->dir,
                                           VHBN_MAX_RANGE * inst->sizeScale, 110.0f,
                                           VHBN_SWEEP_SECS, inst->sizeScale);
            }
            continue;
        }

        inst->sweepTimer += dt;
        float t01 = Clamp(inst->sweepTimer / VHBN_SWEEP_SECS, 0.0f, 1.0f);
        float frontR = (1.2f + (VHBN_MAX_RANGE - 1.2f) * t01) * inst->sizeScale;

        /* Leading-edge Shader-Puff turbulence accent riding the dual-pass FireCrescentWave */
        {
            Vector3 leadPt = Vector3Add(inst->origin, Vector3Scale(inst->dir, frontR));
            leadPt.y = MapManager_GetGroundHeightAt(leadPt.x, leadPt.z) + 0.85f;
            EmitCrescentPuff(leadPt, 0.30f * inst->sizeScale, 0.24f, 45, true);
        }

        inst->emberAccum += dt;
        if (inst->emberAccum >= 0.07f) {
            inst->emberAccum = 0.0f;
            Vector3 midPt = Vector3Add(inst->origin, Vector3Scale(inst->dir, frontR));
            midPt.y = MapManager_GetGroundHeightAt(midPt.x, midPt.z) + 0.95f;
            VFX_ComposeEmberBurst(midPt, inst->dir, VC_MAT_FIRE, 0.80f * inst->sizeScale, 0.65f);
        }

        /* Advancing cone hitbox (dodgeable wave expanding outward from caster) */
        int targets[16];
        int cnt = Entity_GetNearbyTargets(inst->origin, frontR + 0.45f, targets, 16);
        for (int k = 0; k < cnt; k++) {
            int tid = targets[k];
            if (tid == inst->ownerAgentId || AlreadyHit(inst, tid) || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
            const Agent *tgt = Entity_GetAgent(tid);
            if (!tgt) continue;
            Vector3 toT = Vector3Subtract(tgt->position, inst->origin);
            toT.y = 0.0f;
            float dist = Vector3Length(toT);
            if (dist < 0.01f || dist > frontR + 0.45f) continue;
            Vector3 toDir = Vector3Scale(toT, 1.0f / dist);
            if (Vector3DotProduct(toDir, inst->dir) < cosf(VHBN_HALF_CONE)) continue;

            if (inst->hitCount < MAX_VHBN_HIT) inst->hitIds[inst->hitCount++] = tid;
            Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, Vector3Scale(toDir, 3.2f));
            Entity_ApplyStatus(tid, STATUS_BURN, 11.0f / 3.5f, 3.5f, inst->ownerAgentId);
            VoHoc_ApplyFireMark(inst->ownerAgentId, tid, 1);
            VFX_ComposeEmberBurst((Vector3){ tgt->position.x, tgt->position.y + 0.9f, tgt->position.z },
                                  toDir, VC_MAT_FIRE, 0.85f * inst->sizeScale, 0.85f);
            PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
        }

        if (inst->sweepTimer >= VHBN_SWEEP_SECS) {
            inst->active = false;
        }
    }
}

void DrawLhptViemHoBanNguyetCuocSkill(void) { }

void UnloadLhptViemHoBanNguyetCuocSkill(void)
{
    for (int i = 0; i < MAX_VHBN_INST; i++) s_inst[i].active = false;
}
