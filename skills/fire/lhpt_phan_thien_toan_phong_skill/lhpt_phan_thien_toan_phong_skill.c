#include "skills/fire/lhpt_phan_thien_toan_phong_skill/lhpt_phan_thien_toan_phong_skill.h"
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

#define MAX_PTTP_INST   4
#define PTTP_HIT_RADIUS 1.85f
#define PTTP_PEAK_SECS  0.38f /* Tier 3 Peak Frame <= 0.40s (VO_HOC_BALANCE §4.1) */

typedef struct {
    bool    active;
    bool    struck;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   castSecs;
    float   damage;
    float   sizeScale;
    int     cycloneHandle;
} PhanThienInst;

static PhanThienInst s_inst[MAX_PTTP_INST];
static int           s_skillIndex = -1;
static int           s_xichDiemIdx = -1;

SKILL_EMPTY_PROJECTILE_API(LhptPhanThienToanPhong)

/* Every-frame airborne Shader-Puff on kicking feet (no trails, no VolumeTrail) */
static void EmitFootSpinPuff(Vector3 pos, float radius, int count)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne foot fire: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = radius;
    cfg.sourceRadius    = radius * 0.50f;
    cfg.buoyancy        = 3.0f;
    cfg.turbulenceSpeed = 2.4f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = 0.28f;
    cfg.coreHeat        = 1.40f;
    cfg.intensity       = 1.85f;
    cfg.emissiveBoost   = 1.40f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = false;
    VFX_ComposeGuidedFire(pos, &cfg);
}

/* Airborne spinning fire vortex: mandatory Shader-Puff */
static void EmitVortexPuff(Vector3 center, float radius, float life, int count, bool dynLight)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIRE_TORNADO);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne vortex: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_RING;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_VORTEX;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = 0.38f * (radius / PTTP_HIT_RADIUS);
    cfg.sourceRadius    = radius * 0.58f;
    cfg.swirlSpeed      = 16.0f;
    cfg.buoyancy        = 7.2f;
    cfg.turbulenceSpeed = 2.8f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.45f;
    cfg.intensity       = 1.95f;
    cfg.emissiveBoost   = 1.45f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = dynLight;
    VFX_ComposeGuidedFire(center, &cfg);
}

static void AbortLhptPhanThien(int agentId)
{
    for (int i = 0; i < MAX_PTTP_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptPhanThien(int agentId)
{
    for (int i = 0; i < MAX_PTTP_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptPhanThienToanPhongSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_PTTP_INST; i++) s_inst[i].active = false;

    s_skillIndex  = Skill_GetIndexByName("LHPT_PHAN_THIEN_TOAN_PHONG");
    s_xichDiemIdx = Skill_GetIndexByName("LHPT_XICH_DIEM_TRAC_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHAN_THIEN_TOAN_PHONG");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptPhanThien);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptPhanThien);
    }
}

void CastLhptPhanThienToanPhongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_PTTP_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_PHAN_THIEN_TOAN_PHONG");
    float baseDmg = def ? def->baseDamage : 33.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Bước áp sát xoay trục tung Toàn Phong Cước (hurricane_kick) 720° */
    Entity_Dash(agentId, dir, 12.0f);
    Skill_RequestAnim(agentId, "hurricane_kick", 0.72f);

    PhanThienInst *inst = &s_inst[slot];
    *inst = (PhanThienInst){0};
    inst->active       = true;
    inst->struck       = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->dir          = dir;
    inst->castSecs     = def ? def->castSecs : 0.28f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;
    inst->cycloneHandle = VFX_FireCyclone_Spawn(anchor, PTTP_HIT_RADIUS * scale * 0.92f,
                                                2.35f * scale, inst->castSecs + PTTP_PEAK_SECS);

    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

static void TriggerPhanThienImpact(PhanThienInst *inst, Vector3 center)
{
    inst->struck = true;
    float r = PTTP_HIT_RADIUS * inst->sizeScale;

    Vector3 midPos = { center.x, center.y + 0.95f, center.z };
    EmitVortexPuff(midPos, r, 0.45f, 190, true);
    VFX_ComposeFireballBurst(midPos, VC_MAT_FIRE, 1.05f * inst->sizeScale, 0.90f);
    VFX_SpawnGroundWave((Vector3){ center.x, MapManager_GetGroundHeightAt(center.x, center.z) + 0.05f, center.z },
                        VC_MAT_FIRE, r * 1.15f, 0.34f);
    VFX_ComposeEmberBurst(midPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 1.30f * inst->sizeScale, 0.95f);
    Wind_SpawnRadialBlast(midPos, r * 1.25f, 8.5f, 0.22f);

    int targets[12];
    int cnt = Entity_GetNearbyTargets(center, r, targets, 12);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;

        float mult = 1.0f;
        VoHoc_OnPhanThienConsume(inst->ownerAgentId, tid, s_skillIndex, s_xichDiemIdx, &mult);
        float totalDmg = inst->damage * mult;

        const Agent *tgt = Entity_GetAgent(tid);
        Vector3 kbDir = inst->dir;
        if (tgt) {
            Vector3 d = Vector3Subtract(tgt->position, center);
            d.y = 0.0f;
            if (Vector3Length(d) > 0.05f) kbDir = Vector3Normalize(d);
        }

        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, totalDmg, 0.0f, Vector3Scale(kbDir, 2.5f));
        Entity_ApplyStatus(tid, STATUS_BURN, 11.0f / 3.0f, 3.0f, inst->ownerAgentId);
        Entity_ApplyStun(tid, 0.55f);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.055f, 0.07f);
        PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
    }
}

void UpdateLhptPhanThienToanPhongSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_PTTP_INST; i++) {
        PhanThienInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
        Vector3 anchor = owner ? owner->position : inst->origin;
        if (inst->cycloneHandle >= 0) {
            VFX_FireCyclone_SetCenter(inst->cycloneHandle, anchor);
        }

        /* Every-frame Shader-Puff fire whipping directly off both spinning feet */
        if (owner && anim) {
            Vector3 rFoot, lFoot;
            if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                            "mixamorig:RightFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &rFoot)) {
                EmitFootSpinPuff(rFoot, 0.24f * inst->sizeScale, 55);
            }
            if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                            "mixamorig:LeftFoot", (Vector3){ 0.0f, 0.05f, 0.0f }, &lFoot)) {
                EmitFootSpinPuff(lFoot, 0.22f * inst->sizeScale, 50);
            }
        }

        /* Every-frame spinning fire cyclone around caster during active spin */
        if (inst->elapsed >= inst->castSecs * 0.50f && inst->elapsed <= inst->castSecs + PTTP_PEAK_SECS) {
            Vector3 spinPos = { anchor.x, anchor.y + 0.85f, anchor.z };
            EmitVortexPuff(spinPos, PTTP_HIT_RADIUS * inst->sizeScale * 0.88f, 0.36f, 175, true);
        }

        if (!inst->struck && inst->elapsed >= inst->castSecs) {
            TriggerPhanThienImpact(inst, anchor);
        }

        if (inst->elapsed >= inst->castSecs + PTTP_PEAK_SECS) {
            inst->active = false;
        }
    }
}

void DrawLhptPhanThienToanPhongSkill(void) { }

void UnloadLhptPhanThienToanPhongSkill(void)
{
    for (int i = 0; i < MAX_PTTP_INST; i++) s_inst[i].active = false;
}
