#include "skills/fire/lhpt_phan_dia_tram_cuoc_skill/lhpt_phan_dia_tram_cuoc_skill.h"
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

#define MAX_PDTC_INST     4
#define PDTC_MAX_RANGE    8.5f
#define PDTC_RADIUS       3.2f
#define PDTC_PEAK_SECS    0.38f /* Tier 3 Peak Frame <= 0.40s (VO_HOC_BALANCE §4.1) */

typedef struct {
    bool    active;
    bool    erupted;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 targetPos;
    float   elapsed;
    float   castSecs;
    float   peakTimer;
    float   damage;
    float   sizeScale;
} PhanDiaInst;

static PhanDiaInst s_inst[MAX_PDTC_INST];
static int         s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(LhptPhanDiaTramCuoc)

/* Ground-clinging telegraph: Shader-Tongue ONLY when fire hugs terrain */
static void EmitGroundTelegraphTongue(Vector3 groundPos, float radius, float life, int count)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_GROUND_FIRE);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_TONGUE;    /* Ground-clinging -> Shader-Tongue */
    cfg.emitterShape    = VFX_FIRE_EMITTER_RING;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_SURFACE_CLING;  /* Hugs terrain surface */
    cfg.particleCount   = count;
    cfg.particleRadius  = 0.24f;
    cfg.sourceRadius    = radius;
    cfg.buoyancy        = 0.85f;
    cfg.turbulenceSpeed = 1.6f;
    cfg.distortion      = 0.54f;
    cfg.softness        = 0.34f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.45f;
    cfg.intensity       = 2.05f;
    cfg.emissiveBoost   = 1.65f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = false;
    VFX_ComposeGuidedFire(groundPos, &cfg);
}

/* Airborne flip-kick foot arc & rising convective column: mandatory Shader-Puff */
static void EmitAirbornePuff(Vector3 pos, float partRadius, float srcRadius,
                             float buoyancy, float life, int count, bool dynLight)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF;      /* Airborne fire -> mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_CIRCLE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = partRadius;
    cfg.sourceRadius    = srcRadius;
    cfg.riseMultiplier  = 2.6f;
    cfg.buoyancy        = buoyancy;
    cfg.turbulenceSpeed = 3.0f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.45f;
    cfg.intensity       = 1.95f;
    cfg.emissiveBoost   = 1.45f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = dynLight;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void AbortLhptPhanDia(int agentId)
{
    for (int i = 0; i < MAX_PDTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptPhanDia(int agentId)
{
    for (int i = 0; i < MAX_PDTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptPhanDiaTramCuocSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_PDTC_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_PHAN_DIA_TRAM_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHAN_DIA_TRAM_CUOC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptPhanDia);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptPhanDia);
    }
}

void CastLhptPhanDiaTramCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_PDTC_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHAN_DIA_TRAM_CUOC");
    float baseDmg = def ? def->baseDamage : 24.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 delta  = Vector3Subtract(target, anchor);
    delta.y = 0.0f;
    float dist = Vector3Length(delta);
    Vector3 dir = (dist > 0.01f) ? Vector3Scale(delta, 1.0f / dist) : (Vector3){ 0.0f, 0.0f, 1.0f };
    if (dist > PDTC_MAX_RANGE * scale) dist = PDTC_MAX_RANGE * scale;
    if (dist < 1.2f) dist = 1.2f;

    Vector3 tgtGround = Vector3Add(anchor, Vector3Scale(dir, dist));
    tgtGround.y = MapManager_GetGroundHeightAt(tgtGround.x, tgtGround.z) + 0.06f;

    /* Khởi (0.65s >= 0.60s C9): Lộn vòng tung Trầm Cước (flip_kick), mạch lửa bám đất chạy tới đích */
    Skill_RequestAnim(agentId, "flip_kick", 0.88f);

    PhanDiaInst *inst = &s_inst[slot];
    *inst = (PhanDiaInst){0};
    inst->active       = true;
    inst->erupted      = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->targetPos    = tgtGround;
    inst->castSecs     = def ? def->castSecs : 0.65f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    /* Initial ground-clinging Shader-Tongue + ground ripple telegraph */
    VFX_SpawnGroundWave(tgtGround, VC_MAT_FIRE, PDTC_RADIUS * scale * 0.92f, 0.36f);
    EmitGroundTelegraphTongue(tgtGround, PDTC_RADIUS * scale * 0.88f, 0.36f, 70);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

static void TriggerPhanDiaEruption(PhanDiaInst *inst)
{
    inst->erupted   = true;
    inst->peakTimer = 0.0f;
    float r = PDTC_RADIUS * inst->sizeScale;

    /* Phát: Cột lửa đối lưu + FireballBurst + Scorch crater decal + GroundWave */
    Vector3 liftPos = { inst->targetPos.x, inst->targetPos.y + 0.45f, inst->targetPos.z };
    EmitAirbornePuff(liftPos, 0.46f * inst->sizeScale, r * 0.55f, 10.5f, 0.48f, 180, true);
    VFX_ComposeFireballBurst(liftPos, VC_MAT_FIRE, 1.25f * inst->sizeScale, 0.95f);
    VFX_ComposeDecalVariant(inst->targetPos, VC_MAT_FIRE, r * 0.95f, 0.90f, 1.2f, VFX_DECAL_VARIANT_SCORCH);
    VFX_SpawnGroundWave(inst->targetPos, VC_MAT_FIRE, r * 1.15f, 0.38f);
    VFX_FireKickArc_Spawn(liftPos, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f },
                          1.35f * inst->sizeScale, 150.0f, 0.28f, inst->sizeScale);
    EmitGroundTelegraphTongue(inst->targetPos, r * 0.92f, 0.50f, 80);
    VFX_ComposeEmberBurst(liftPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 1.35f * inst->sizeScale, 0.95f);
    VFX_ComposeImpactDust(inst->targetPos, VC_MAT_FIRE, 1.00f * inst->sizeScale, 0.65f);
    Wind_SpawnRadialBlast(liftPos, r * 1.15f, 9.0f, 0.25f);

    int targets[16];
    int cnt = Entity_GetNearbyTargets(inst->targetPos, r, targets, 16);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplyStatus(tid, STATUS_BURN, 18.0f / 3.5f, 3.5f, inst->ownerAgentId);
        /* 0.70s airtime Launch: v_y = 0.5 * 9.81 * 0.70 ~= 3.43 m/s */
        Entity_ApplyLaunch(tid, 3.43f, (Vector3){ 0.0f, 0.0f, 0.0f });
        VoHoc_ApplyFireMark(inst->ownerAgentId, tid, 2);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.050f, 0.08f);
    }
    PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateLhptPhanDiaTramCuocSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_PDTC_INST; i++) {
        PhanDiaInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);

        if (!inst->erupted) {
            /* Airborne Shader-Puff fire arc on the flipping foot */
            if (owner && anim) {
                Vector3 footPos;
                if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                                "mixamorig:RightFoot", (Vector3){ 0.0f, 0.06f, 0.0f }, &footPos)) {
                    EmitAirbornePuff(footPos, 0.24f * inst->sizeScale, 0.14f * inst->sizeScale,
                                     2.8f, 0.24f, 60, false);
                }
            }

            /* Every-frame ground-clinging Shader-Tongue vein + expanding target ring */
            float prog = Clamp(inst->elapsed / inst->castSecs, 0.0f, 1.0f);
            Vector3 creepPos = Vector3Lerp(inst->origin, inst->targetPos, prog);
            creepPos.y = MapManager_GetGroundHeightAt(creepPos.x, creepPos.z) + 0.05f;
            EmitGroundTelegraphTongue(creepPos, 0.38f * inst->sizeScale, 0.24f, 36);
            EmitGroundTelegraphTongue(inst->targetPos, PDTC_RADIUS * inst->sizeScale * (0.45f + 0.45f * prog), 0.26f, 64);

            if (inst->elapsed >= inst->castSecs) {
                TriggerPhanDiaEruption(inst);
            }
            continue;
        }

        inst->peakTimer += dt;
        if (inst->peakTimer < PDTC_PEAK_SECS) {
            float liftH = 0.55f + 1.90f * (inst->peakTimer / PDTC_PEAK_SECS);
            Vector3 colPos = { inst->targetPos.x, inst->targetPos.y + liftH, inst->targetPos.z };
            EmitAirbornePuff(colPos, 0.44f * inst->sizeScale, PDTC_RADIUS * inst->sizeScale * 0.42f,
                             10.5f, 0.36f, 145, true);
        } else {
            inst->active = false;
        }
    }
}

void DrawLhptPhanDiaTramCuocSkill(void)
{
    for (int i = 0; i < MAX_PDTC_INST; i++) {
        const PhanDiaInst *inst = &s_inst[i];
        if (!inst->active || inst->erupted) continue;
        float prog = Clamp(inst->elapsed / fmaxf(0.05f, inst->castSecs), 0.0f, 1.0f);
        VFX_ComposeRuneCircle(inst->targetPos, (Vector3){ 0.0f, 1.0f, 0.0f },
                              VC_MAT_FIRE, PDTC_RADIUS * inst->sizeScale, prog, 1);
    }
}

void UnloadLhptPhanDiaTramCuocSkill(void)
{
    for (int i = 0; i < MAX_PDTC_INST; i++) s_inst[i].active = false;
}
