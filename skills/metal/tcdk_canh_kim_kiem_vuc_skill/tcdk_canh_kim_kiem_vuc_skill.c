#include "skills/metal/tcdk_canh_kim_kiem_vuc_skill/tcdk_canh_kim_kiem_vuc_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
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

#define MAX_CKKV_INST 4
#define CKKV_RADIUS   3.8f
#define CKKV_DURATION 5.0f

typedef struct {
    bool      active;
    int       ownerAgentId;
    AgentTeam allyTeam;
    Vector3   center;
    float     elapsed;
    float     tickAccum;
    float     markAccum;
    float     sizeScale;
    int       zoneHandle;
} CanhKimInst;

static CanhKimInst s_inst[MAX_CKKV_INST];
static int         s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkCanhKimKiemVuc)

static void KillCanhKimHandles(CanhKimInst *inst)
{
    if (inst->zoneHandle > 0) {
        VFX_MetalMagneticZone_Stop(inst->zoneHandle);
        inst->zoneHandle = -1;
    }
}

static void AbortTcdkCanhKim(int agentId)
{
    for (int i = 0; i < MAX_CKKV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillCanhKimHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkCanhKim(int agentId)
{
    for (int i = 0; i < MAX_CKKV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkCanhKimKiemVucSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_CKKV_INST; i++) {
        s_inst[i].active     = false;
        s_inst[i].zoneHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TCDK_CANH_KIM_KIEM_VUC");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_CANH_KIM_KIEM_VUC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkCanhKim);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkCanhKim);
    }
}

void CastTcdkCanhKimKiemVucSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_CKKV_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillCanhKimHandles(&s_inst[slot]);

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 center = owner ? owner->position : startPos;
    center.y = MapManager_GetGroundHeightAt(center.x, center.z);

    /* Khởi: Cắm mũi kiếm xuống đất kích hoạt dòng từ trường */
    Skill_RequestAnim(agentId, "great_sword_impact", 0.35f);

    CanhKimInst *inst = &s_inst[slot];
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->allyTeam     = owner ? owner->team : TEAM_ALLY;
    inst->center       = center;
    inst->elapsed      = 0.0f;
    inst->tickAccum    = 0.50f;
    inst->markAccum    = 0.50f;
    inst->sizeScale    = scale;
    inst->zoneHandle   = VFX_MetalMagneticZone_Spawn(center, CKKV_RADIUS * scale, CKKV_DURATION);
PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkCanhKimKiemVucSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_CKKV_INST; i++) {
        CanhKimInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed   += dt;
        inst->tickAccum += dt;
        inst->markAccum += dt;
        float r = CKKV_RADIUS * inst->sizeScale;

        if (inst->tickAccum >= 0.50f) {
            inst->tickAccum -= 0.50f;
            VFX_MetalMagneticZone_SetPulse(inst->zoneHandle, 1.0f);

            int allies[16];
            int allyCnt = Entity_GetNearbyTargetsTeam(inst->center, r, inst->allyTeam, allies, 16);
            for (int k = 0; k < allyCnt; k++) {
                Entity_RestoreMana(allies[k], 3.0f);               /* 6 Mana/s */
                SkillManager_ReduceAgentCooldowns(allies[k], 0.15f); /* 1.5s CDR over 5s */
                Entity_AddModifier(allies[k], 1.15f, 0.75f);        /* +15% speed */
            }

            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    Entity_ApplyStatus(enemies[k], STATUS_WEAKEN, 0.15f, 0.75f, inst->ownerAgentId);
                }
            }
        }

        if (inst->markAccum >= 2.50f) {
            inst->markAccum -= 2.50f;
            int enemies[16];
            int enemyCnt = Entity_GetNearbyTargets(inst->center, r, enemies, 16);
            for (int k = 0; k < enemyCnt; k++) {
                if (enemies[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, enemies[k])) {
                    VoHoc_ApplyMetalMark(inst->ownerAgentId, enemies[k], 1);
                }
            }
        }

        if (inst->elapsed >= CKKV_DURATION) {
            KillCanhKimHandles(inst);
            inst->active = false;
        }
    }
}

void DrawTcdkCanhKimKiemVucSkill(void) { }

void UnloadTcdkCanhKimKiemVucSkill(void)
{
    for (int i = 0; i < MAX_CKKV_INST; i++) {
        KillCanhKimHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
