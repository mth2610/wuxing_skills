#include "skills/metal/tcdk_kim_cang_phan_chan_skill/tcdk_kim_cang_phan_chan_skill.h"
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

#define MAX_KCPC_INST 4

typedef struct {
    bool  active;
    int   ownerAgentId;
    float elapsed;
    float duration;
    float radius;
    int   deflectsRemaining;
    int   guardHandle;
} KimCangMetalInst;

static KimCangMetalInst s_inst[MAX_KCPC_INST];
static int              s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkKimCangPhanChan)

static void KillKimCangHandles(KimCangMetalInst *inst)
{
    if (inst->guardHandle > 0) {
        VFX_MetalParryGuard_Stop(inst->guardHandle);
        inst->guardHandle = -1;
    }
}

static void AbortTcdkKimCang(int agentId)
{
    for (int i = 0; i < MAX_KCPC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillKimCangHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkKimCang(int agentId)
{
    for (int i = 0; i < MAX_KCPC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkKimCangPhanChanSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_KCPC_INST; i++) {
        s_inst[i].active      = false;
        s_inst[i].guardHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TCDK_KIM_CANG_PHAN_CHAN");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_KIM_CANG_PHAN_CHAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkKimCang);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkKimCang);
    }
}

void CastTcdkKimCangPhanChanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_KCPC_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillKimCangHandles(&s_inst[slot]);

    float scale     = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult   = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float shieldAmt = 30.0f * pveMult;

    /* Khởi & Phát: Dựng dọc thanh kiếm trước ngực -> Giải CC, Khiên 30 HP (4s), Tăng tốc 25% (2.5s) */
    Skill_RequestAnim(agentId, "sword_and_shield_casting", 0.35f);
    Entity_Cleanse(agentId);
    Entity_AddShield(agentId, shieldAmt, 4.0f);
    Entity_AddModifier(agentId, 1.25f, 2.5f);

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;

    KimCangMetalInst *inst = &s_inst[slot];
    inst->active            = true;
    inst->ownerAgentId      = agentId;
    inst->elapsed           = 0.0f;
    inst->duration          = 4.0f;
    inst->radius            = 1.8f * scale;
    inst->deflectsRemaining = 2;
    inst->guardHandle       = VFX_MetalParryGuard_Spawn(anchor, inst->radius, inst->duration);

    VFX_ComposeCleanseBurst(anchor, VC_MAT_METAL, 1.45f * scale);
    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkKimCangPhanChanSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_KCPC_INST; i++) {
        KimCangMetalInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner || !owner->active || inst->elapsed >= inst->duration) {
            KillKimCangHandles(inst);
            inst->active = false;
            continue;
        }

        Vector3 fwd = { sinf(owner->facingYaw), 0.0f, cosf(owner->facingYaw) };
        VFX_MetalParryGuard_SetTransform(inst->guardHandle, owner->position, fwd);

        /* Elastic parry deflect: up to 2 enemy projectiles at exact intercept point */
        if (inst->deflectsRemaining > 0) {
            CombatDeflectInfo defs[4];
            int broken = Combat_DeflectProjectilesInRadiusEx(owner->position, inst->radius,
                                                             inst->ownerAgentId, inst->deflectsRemaining, defs, 4);
            if (broken > 0) {
                Skill_RequestAnim(inst->ownerAgentId, "sword_and_shield_slash_2", 0.25f);
                for (int d = 0; d < broken && inst->deflectsRemaining > 0; d++) {
                    inst->deflectsRemaining--;
                    Vector3 hitPt = defs[d].pos;
                    Vector3 nrm = Vector3Subtract(hitPt, owner->position);
                    nrm.y = 0.0f;
                    nrm = (Vector3Length(nrm) > 0.01f) ? Vector3Normalize(nrm) : fwd;
                    VFX_MetalParryGuard_TriggerDeflect(inst->guardHandle, hitPt);
                    VFX_ComposeDeflectFlash(hitPt, nrm, VC_MAT_METAL, 0.95f);
                }
                PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
            }
        }
    }
}

void DrawTcdkKimCangPhanChanSkill(void) { }

void UnloadTcdkKimCangPhanChanSkill(void)
{
    for (int i = 0; i < MAX_KCPC_INST; i++) {
        KillKimCangHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
