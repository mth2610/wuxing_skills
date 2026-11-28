#include "skills/metal/tcdk_chan_am_kiem_tran_skill/tcdk_chan_am_kiem_tran_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CAKT_INST      4
#define CAKT_RADIUS        2.8f
#define CAKT_TELEGRAPH_SEC 0.65f
#define CAKT_ERUPT_SEC     0.35f

typedef struct {
    bool    active;
    bool    erupted;
    int     ownerAgentId;
    Vector3 targetPos;
    float   elapsed;
    float   damage;
    float   sizeScale;
    int     arrayHandle;
} ChanAmInst;

static ChanAmInst s_inst[MAX_CAKT_INST];
static int        s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkChanAmKiemTran)

static void KillChanAmHandles(ChanAmInst *inst)
{
    if (inst->arrayHandle > 0) {
        VFX_MetalResonanceArray_Kill(inst->arrayHandle);
        inst->arrayHandle = -1;
    }
}

static void AbortTcdkChanAm(int agentId)
{
    for (int i = 0; i < MAX_CAKT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillChanAmHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkChanAm(int agentId)
{
    for (int i = 0; i < MAX_CAKT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkChanAmKiemTranSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_CAKT_INST; i++) {
        s_inst[i].active      = false;
        s_inst[i].arrayHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TCDK_CHAN_AM_KIEM_TRAN");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_CHAN_AM_KIEM_TRAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkChanAm);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkChanAm);
    }
}

void CastTcdkChanAmKiemTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_CAKT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        KillChanAmHandles(&s_inst[0]);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_CHAN_AM_KIEM_TRAN");
    float baseDmg = def ? def->baseDamage : 22.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 casterPos = owner ? owner->position : startPos;
    Vector3 tgt = target;
    tgt.y = MapManager_GetGroundHeightAt(tgt.x, tgt.z);

    /* Khởi (C9 Telegraph 0.65s): Dựng kiếm bắt quyết truyền xung chấn âm */
    Skill_RequestAnim(agentId, "wuxia_sword_cast", 0.55f);

    ChanAmInst *inst = &s_inst[slot];
    inst->active       = true;
    inst->erupted      = false;
    inst->ownerAgentId = agentId;
    inst->targetPos    = tgt;
    inst->elapsed      = 0.0f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;
    inst->arrayHandle  = VFX_MetalResonanceArray_Spawn(casterPos, tgt, CAKT_RADIUS * scale,
                                                       CAKT_TELEGRAPH_SEC, CAKT_ERUPT_SEC);
    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

static void TriggerChanAmEruption(ChanAmInst *inst)
{
    inst->erupted = true;
    Skill_RequestAnim(inst->ownerAgentId, "wuxia_sword_sky_summon", 0.50f);
    float r = CAKT_RADIUS * inst->sizeScale;

    /* Phát: Kiếm khí cộng hưởng bật trồi theo phương thẳng đứng (Launch 0.60s, Silence 0.80s, +2 Ấn) */
    int targets[16];
    int cnt = Entity_GetNearbyTargets(inst->targetPos, r, targets, 16);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
        /* 0.60s airtime: v_y = 0.5 * 9.81 * 0.60 = 2.94 m/s */
        Entity_ApplyLaunch(tid, 2.94f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplySilence(tid, 0.80f);
        VoHoc_ApplyMetalMark(inst->ownerAgentId, tid, 2);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.055f, 0.08f);
    }
VFX_SpawnShockRing((Vector3){ inst->targetPos.x, inst->targetPos.y + 0.35f, inst->targetPos.z },
                       (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_METAL, r, 0.34f);
    PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkChanAmKiemTranSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_CAKT_INST; i++) {
        ChanAmInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->erupted && inst->elapsed >= CAKT_TELEGRAPH_SEC) {
            TriggerChanAmEruption(inst);
        }
        if (inst->elapsed >= CAKT_TELEGRAPH_SEC + CAKT_ERUPT_SEC + 0.25f) {
            inst->arrayHandle = -1;
            inst->active      = false;
        }
    }
}

void DrawTcdkChanAmKiemTranSkill(void) { }

void UnloadTcdkChanAmKiemTranSkill(void)
{
    for (int i = 0; i < MAX_CAKT_INST; i++) {
        KillChanAmHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
