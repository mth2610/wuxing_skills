#include "skills/earth/btbc_ban_thach_kim_cang_giap_skill/btbc_ban_thach_kim_cang_giap_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "core/composition/visual_composer.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BTKCG_INST 4

typedef struct {
    bool  active;
    int   ownerAgentId;
    float elapsed;
    float duration;
    float maxShield;
    int   deflectsRemaining;
    int   armorHandle;
} KimCangInst;

static KimCangInst s_inst[MAX_BTKCG_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcBanThachKimCangGiap)

static void KillKimCangHandles(KimCangInst *inst) {
    if (inst->armorHandle >= 0) {
        VFX_StoneArmor_Stop(inst->armorHandle);
        inst->armorHandle = -1;
    }
}

static void AbortBtbcKimCang(int agentId) {
    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillKimCangHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcKimCang(int agentId) {
    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcBanThachKimCangGiapSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].armorHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("BTBC_BAN_THACH_KIM_CANG_GIAP");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_BAN_THACH_KIM_CANG_GIAP");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcKimCang);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcKimCang);
    }
}

void CastBtbcBanThachKimCangGiapSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillKimCangHandles(&s_inst[slot]);

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float shieldAmt = 39.0f * pveMult;

    /* Cleanse + Self 36 HP Shield (4.5s) */
    Skill_RequestAnim(agentId, "mutant_roaring", 0.35f);
    Entity_Cleanse(agentId);
    Entity_AddShield(agentId, shieldAmt, 4.5f);

    /* Nearby allies within 4.5m receive 50% shield */
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    if (owner) {
        int allies[16];
        int cnt = Entity_GetNearbyTargetsTeam(owner->position, 4.5f * scale, owner->team, allies, 16);
        for (int k = 0; k < cnt; k++) {
            if (allies[k] == agentId) continue;
            Entity_AddShield(allies[k], shieldAmt * 0.50f, 4.5f);
        }
    }

    KimCangInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->elapsed = 0.0f;
    inst->duration = 4.5f;
    inst->maxShield = shieldAmt;
    inst->deflectsRemaining = 2;
    inst->armorHandle = VFX_StoneArmor_Spawn(anchor, (Vector3){ 0.0f, 0.0f, 1.0f },
                                             VFX_STONE_ARMOR_FULL_BODY, scale, 4.5f);

    VFX_ComposeGroundDustRing(anchor, VC_MAT_EARTH, 0.75f * scale, 0.35f);
    VFX_ComposeCleanseBurst(anchor, VC_MAT_EARTH, 1.50f * scale);
PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateBtbcBanThachKimCangGiapSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        KimCangInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner || !owner->active || inst->elapsed >= inst->duration) {
            KillKimCangHandles(inst);
            inst->active = false;
            continue;
        }

        VFX_StoneArmor_SetTransform(inst->armorHandle, owner->position, (Vector3){ 0.0f, 0.0f, 1.0f });

        /* Reactive shatter: plates break off as remaining shield drops */
        float curShield = Entity_GetShield(inst->ownerAgentId);
        int plates = (int)ceilf(6.0f * (curShield / fmaxf(1.0f, inst->maxShield)));
        if (plates < 0) plates = 0;
        if (plates > 6) plates = 6;
        VFX_StoneArmor_SetActivePlates(inst->armorHandle, plates);

        if (plates == 0 && inst->elapsed > 0.25f) {
            KillKimCangHandles(inst);
            inst->active = false;
            continue;
        }

        /* Deflect up to 2 enemy projectiles around caster at exact intercept point */
        if (inst->deflectsRemaining > 0) {
            CombatDeflectInfo defs[4];
            int broken = Combat_DeflectProjectilesInRadiusEx(owner->position, 1.85f, inst->ownerAgentId,
                                                             inst->deflectsRemaining, defs, 4);
            if (broken > 0) {
                for (int d = 0; d < broken && inst->deflectsRemaining > 0; d++) {
                    inst->deflectsRemaining--;
                    Vector3 hitPos = defs[d].pos;
                    Vector3 nrm = Vector3Subtract(hitPos, owner->position);
                    nrm.y = 0.0f;
                    nrm = (Vector3Length(nrm) > 0.01f) ? Vector3Normalize(nrm) : (Vector3){ 0.0f, 1.0f, 0.0f };
                    VFX_ComposeDeflectFlash(hitPos, nrm, VC_MAT_EARTH, 0.90f);
                    VFX_ComposeDebrisShards(hitPos, (Vector3){ nrm.x * 2.2f, 2.5f, nrm.z * 2.2f }, VC_MAT_EARTH, 0.045f, 4);
                }
                PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
            }
        }
    }
}

void DrawBtbcBanThachKimCangGiapSkill(void) { }

void UnloadBtbcBanThachKimCangGiapSkill(void) {
    for (int i = 0; i < MAX_BTKCG_INST; i++) {
        if (s_inst[i].active) KillKimCangHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
