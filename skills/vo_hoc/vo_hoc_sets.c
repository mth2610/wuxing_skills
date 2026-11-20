#include "skills/vo_hoc/vo_hoc_sets.h"
#include "entities/entities.h"
#include "skills/skill_manager.h"
#include "raylib.h"

/* Per-agent ICD for Đại Thành Nộ Hải cooldown reset (10s) */
static double s_noHaiResetLastTime[MAX_AGENTS];

int VoHoc_GetAgentSetTier(int agentId, VoHocSetId setId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a) return 0;
    int pieces = VoHoc_CountSetPieces(a->equippedSkills, AGENT_SKILL_SLOTS, setId);
    if (pieces >= VO_HOC_TIER_MAJOR_COUNT) return VO_HOC_TIER_MAJOR_COUNT;
    if (pieces >= VO_HOC_TIER_MINOR_COUNT) return VO_HOC_TIER_MINOR_COUNT;
    return 0;
}

bool VoHoc_ApplyWaterMark(int casterId, int targetId, int stacks) {
    if (stacks <= 0) return false;
    int tier = VoHoc_GetAgentSetTier(casterId, VH_SET_THUY_TRIEU_QUYEN_PHAP);
    float duration = (tier >= VO_HOC_TIER_MINOR_COUNT) ? 6.0f : 4.0f;

    /* Tương Sinh: Kim (4) -> Thủy (0): if target has Metal mark, consume it for +1 Triều Ấn */
    int bonus = 0;
    if (Entity_GetMarkStacks(targetId, 4) > 0) {
        Entity_ConsumeMarks(targetId, 4);
        bonus = 1;
    }

    int total = Entity_AddMark(targetId, 0, stacks + bonus, 3, duration);
    if (total >= 3) {
        Entity_ConsumeMarks(targetId, 0);
        Entity_ApplyFreeze(targetId, 0.75f);
        Entity_SetMarkImmune(targetId, 0, 3.0f);
        if (tier >= VO_HOC_TIER_MINOR_COUNT) {
            Entity_RestoreMana(casterId, 10.0f);
        }
        return true;
    }
    return false;
}

float VoHoc_OnNoHaiConsume(int casterId, int targetId, int noHaiSkillIndex, float *outMult) {
    int consumed = Entity_ConsumeMarks(targetId, 0);
    if (outMult) {
        *outMult = 1.0f + 0.12f * (float)consumed;
    }
    float bonusDmg = 0.0f;
    if (consumed >= 3 && VoHoc_GetAgentSetTier(casterId, VH_SET_THUY_TRIEU_QUYEN_PHAP) >= VO_HOC_TIER_MAJOR_COUNT) {
        bonusDmg = 10.0f; /* Phá Băng */
        if (casterId >= 0 && casterId < MAX_AGENTS) {
            double now = GetTime();
            if (s_noHaiResetLastTime[casterId] <= 0.0 || (now - s_noHaiResetLastTime[casterId]) >= 10.0) {
                s_noHaiResetLastTime[casterId] = now;
                float baseCd = Skill_GetCooldown(noHaiSkillIndex);
                SkillManager_TriggerCooldown(noHaiSkillIndex, casterId, baseCd * 0.5f);
            }
        }
    }
    return bonusDmg;
}
