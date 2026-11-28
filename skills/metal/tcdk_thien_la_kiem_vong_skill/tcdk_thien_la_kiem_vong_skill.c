#include "skills/metal/tcdk_thien_la_kiem_vong_skill/tcdk_thien_la_kiem_vong_skill.h"
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

#define MAX_TLKV_INST      4
#define MAX_TLKV_HIT       24
#define TLKV_RANGE         5.5f
#define TLKV_HALF_CONE_RAD (37.5f * DEG2RAD)
#define TLKV_SPEED         15.5f

typedef struct {
    bool    active;
    int     swingStage; /* 0 = wind-up, 1..3 = sequential sword swings */
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   castSecs;
    float   frontDist;
    float   damage;
    float   sizeScale;
    int     hitIds[MAX_TLKV_HIT];
    int     hitCount;
} ThienLaInst;

static ThienLaInst s_inst[MAX_TLKV_INST];
static int         s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkThienLaKiemVong)

static bool AlreadyHit(const ThienLaInst *inst, int agentId)
{
    for (int i = 0; i < inst->hitCount; i++) {
        if (inst->hitIds[i] == agentId) return true;
    }
    return false;
}

static void AbortTcdkThienLa(int agentId)
{
    for (int i = 0; i < MAX_TLKV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkThienLa(int agentId)
{
    for (int i = 0; i < MAX_TLKV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkThienLaKiemVongSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TLKV_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("TCDK_THIEN_LA_KIEM_VONG");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_THIEN_LA_KIEM_VONG");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkThienLa);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkThienLa);
    }
}

void CastTcdkThienLaKiemVongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_TLKV_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_THIEN_LA_KIEM_VONG");
    float baseDmg = def ? def->baseDamage : 16.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Hạ trọng tâm, rút kiếm chuẩn bị chuỗi 3 nhát chém liên hoàn */
    Skill_RequestAnim(agentId, "wuxia_sword_draw", 0.14f);

    ThienLaInst *inst = &s_inst[slot];
    *inst = (ThienLaInst){0};
    inst->active       = true;
    inst->swingStage   = 0;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->dir          = dir;
    inst->castSecs     = 0.10f;
    inst->frontDist    = 0.35f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

static void FireSwordSwingBeat(ThienLaInst *inst, int beat)
{
    const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
    Vector3 anchor = owner ? owner->position : inst->origin;
    inst->origin = anchor;
    float range  = TLKV_RANGE * inst->sizeScale;

    Vector3 vfxOrigin = anchor;
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
    if (owner && anim) {
        Vector3 handPos;
        if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                        "mixamorig:RightHand", (Vector3){ 0.0f, 0.15f, 0.0f }, &handPos)) {
            vfxOrigin.x = handPos.x;
            vfxOrigin.z = handPos.z;
        }
    }

    if (beat == 1) {
        /* Beat 1: Forehand Diagonal Slash (+32 deg, Left-to-Right) */
        Skill_RequestAnim(inst->ownerAgentId, "wuxia_sword_slash_l2r", 0.22f);
        VFX_MetalCrescentWave_Spawn(vfxOrigin, inst->dir, 32.0f, range * 0.92f, 65.0f, 0.40f, inst->sizeScale);
    } else if (beat == 2) {
        /* Beat 2: Backhand Counter-Diagonal Slash (-32 deg, Right-to-Left) */
        Skill_RequestAnim(inst->ownerAgentId, "wuxia_sword_slash_r2l", 0.22f);
        VFX_MetalCrescentWave_Spawn(vfxOrigin, inst->dir, -32.0f, range * 0.95f, 68.0f, 0.40f, inst->sizeScale);
    } else {
        /* Beat 3: Master Horizontal Wide Cleave (0 deg, 75 deg cone finisher) */
        Skill_RequestAnim(inst->ownerAgentId, "wuxia_sword_cross_x_slash", 0.36f);
        VFX_MetalCrescentWave_Spawn(vfxOrigin, inst->dir, 0.0f, range, 75.0f, 0.44f, inst->sizeScale);
        Wind_SpawnGust(vfxOrigin, inst->dir, 2.2f * inst->sizeScale, 8.5f, 0.36f);
    }
    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkThienLaKiemVongSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TLKV_INST; i++) {
        ThienLaInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;

        /* Sequential 3-beat sword-swing schedule: never spawn multiple slashes simultaneously */
        if (inst->swingStage == 0 && inst->elapsed >= 0.10f) {
            inst->swingStage = 1;
            FireSwordSwingBeat(inst, 1);
        } else if (inst->swingStage == 1 && inst->elapsed >= 0.22f) {
            inst->swingStage = 2;
            FireSwordSwingBeat(inst, 2);
        } else if (inst->swingStage == 2 && inst->elapsed >= 0.36f) {
            inst->swingStage = 3;
            FireSwordSwingBeat(inst, 3);
        }

        if (inst->swingStage == 0) continue;

        float activeTime = inst->elapsed - 0.10f;
        float maxR = TLKV_RANGE * inst->sizeScale;
        inst->frontDist = fminf(maxR, 0.45f + activeTime * TLKV_SPEED);

        int targets[16];
        int cnt = Entity_GetNearbyTargets(inst->origin, inst->frontDist + 0.5f, targets, 16);
        for (int k = 0; k < cnt; k++) {
            int tid = targets[k];
            if (tid == inst->ownerAgentId || AlreadyHit(inst, tid) || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
            const Agent *tgt = Entity_GetAgent(tid);
            if (!tgt) continue;
            Vector3 toT = Vector3Subtract(tgt->position, inst->origin);
            toT.y = 0.0f;
            float dist = Vector3Length(toT);
            if (dist < 0.05f || dist > inst->frontDist + 0.35f) continue;
            Vector3 nDir = Vector3Scale(toT, 1.0f / dist);
            if (Vector3DotProduct(inst->dir, nDir) >= cosf(TLKV_HALF_CONE_RAD)) {
                if (inst->hitCount < MAX_TLKV_HIT) inst->hitIds[inst->hitCount++] = tid;
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
                Entity_AddModifier(tid, 0.65f, 1.5f); /* Làm chậm 35% (1.5s) */
                Entity_ApplyStatus(tid, STATUS_BLEED, 12.0f / 3.0f, 3.0f, inst->ownerAgentId);
                VoHoc_ApplyMetalMark(inst->ownerAgentId, tid, 1);
                VFX_ComposeContactSparkMode(tgt->position, VC_MAT_METAL, 0.60f * inst->sizeScale, 0.75f, CONTACT_SPARK_CENTRIFUGAL);
                PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
            }
        }

        if (inst->elapsed >= 0.78f) {
            inst->active = false;
        }
    }
}

void DrawTcdkThienLaKiemVongSkill(void) { }

void UnloadTcdkThienLaKiemVongSkill(void)
{
    for (int i = 0; i < MAX_TLKV_INST; i++) s_inst[i].active = false;
}
