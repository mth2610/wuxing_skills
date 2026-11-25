#include "skills/earth/btbc_hau_tho_tran_nhac_tran_skill/btbc_hau_tho_tran_nhac_tran_skill.h"
#include "skills/skill_boilerplate.h"
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

#define MAX_HTTNT_INST 4
#define HTTNT_RADIUS   4.0f
#define HTTNT_DURATION 5.0f

typedef struct {
    bool      active;
    int       ownerAgentId;
    AgentTeam allyTeam;
    Vector3   center;
    float     elapsed;
    float     tickAccum;
    float     markAccum;
    float     healPerTick;
    float     shieldPerTick;
    float     sizeScale;
    int       zoneHandle;
} HauThoInst;

static HauThoInst s_inst[MAX_HTTNT_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcHauThoTranNhacTran)

static void KillHauThoHandles(HauThoInst *inst) {
    if (inst->zoneHandle >= 0) {
        VFX_StoneFormationZone_Stop(inst->zoneHandle);
        inst->zoneHandle = -1;
    }
}

static void AbortBtbcHauTho(int agentId) {
    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillHauThoHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcHauTho(int agentId) {
    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcHauThoTranNhacTranSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].zoneHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("BTBC_HAU_THO_TRAN_NHAC_TRAN");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_HAU_THO_TRAN_NHAC_TRAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcHauTho);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcHauTho);
    }
}

void CastBtbcHauThoTranNhacTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillHauThoHandles(&s_inst[slot]);

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 center = owner ? owner->position : startPos;
    center.y = MapManager_GetGroundHeightAt(center.x, center.z);

    HauThoInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->allyTeam = owner ? owner->team : TEAM_ALLY;
    inst->center = center;
    inst->elapsed = 0.0f;
    inst->tickAccum = 0.50f;
    inst->markAccum = 0.50f;
    inst->healPerTick = 3.0f * pveMult;   /* 6 HP/s */
    inst->shieldPerTick = 3.0f * pveMult; /* 6 HP/s */
    inst->sizeScale = scale;
    inst->zoneHandle = VFX_StoneFormationZone_Spawn(center, HTTNT_RADIUS * scale, HTTNT_DURATION);

    Skill_RequestAnim(agentId, "standing_2h_cast_spell_01", 0.40f);
    VFX_ComposeGroundDustRing(center, VC_MAT_EARTH, 1.4f * scale, 0.35f);
    VFX_SpawnGroundWave(center, VC_MAT_EARTH, HTTNT_RADIUS * scale, 0.42f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateBtbcHauThoTranNhacTranSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        HauThoInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        inst->tickAccum += dt;
        inst->markAccum += dt;
        float r = HTTNT_RADIUS * inst->sizeScale;

        if (inst->tickAccum >= 0.50f) {
            inst->tickAccum -= 0.50f;
            VFX_StoneFormationZone_SetPulse(inst->zoneHandle, 1.0f);

            int allies[16];
            int allyCnt = Entity_GetNearbyTargetsTeam(inst->center, r, inst->allyTeam, allies, 16);
            for (int k = 0; k < allyCnt; k++) {
                Entity_Heal(allies[k], inst->healPerTick);
                Entity_AddShield(allies[k], inst->shieldPerTick, 1.5f);
            }

            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    Entity_AddModifier(enemies[k], 0.70f, 0.65f); /* 30% Slow */
                }
            }
        }

        if (inst->markAccum >= 2.50f) {
            inst->markAccum -= 2.50f;
            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    VoHoc_ApplyEarthMark(inst->ownerAgentId, enemies[k], 1);
                }
            }
        }

        if (inst->elapsed >= HTTNT_DURATION) {
            KillHauThoHandles(inst);
            inst->active = false;
        }
    }
}

void DrawBtbcHauThoTranNhacTranSkill(void) { }

void UnloadBtbcHauThoTranNhacTranSkill(void) {
    for (int i = 0; i < MAX_HTTNT_INST; i++) {
        if (s_inst[i].active) KillHauThoHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
