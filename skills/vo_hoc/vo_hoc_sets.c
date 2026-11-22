#include "skills/vo_hoc/vo_hoc_sets.h"
#include "entities/entities.h"
#include "skills/skill_manager.h"
#include "core/composition/visual_composer.h"
#include "raylib.h"

/* Per-agent ICD for Đại Thành Nộ Hải cooldown reset (10s) */
static double s_noHaiResetLastTime[MAX_AGENTS];
/* Per-agent ICD for Tiểu Thành Tụ Thạch Thành Sơn (6s) & Đại Thành Bất Động Như Sơn (10s) */
static double s_tuThachLastTime[MAX_AGENTS];
static double s_batDongLastTime[MAX_AGENTS];
/* Per-agent ICD for Đại Thành Vạn Kiếm Quy Tông (10s) */
static double s_nhatKiemResetLastTime[MAX_AGENTS];

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

    /* Tương Sinh: Kim (4) -> Thủy (0): if target has Metal mark, consume 1 stack for
     * +1 Triều Ấn, -1.0s cooldown on caster's Water skills, and +15% haste burst */
    int bonus = 0;
    if (Entity_GetMarkStacks(targetId, 4) > 0) {
        Entity_ConsumeMarks(targetId, 4);
        SkillManager_ReduceAgentCooldowns(casterId, 1.0f);
        Entity_AddModifier(casterId, 1.15f, 1.5f);
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

bool VoHoc_ApplyEarthMark(int casterId, int targetId, int stacks) {
    if (stacks <= 0) return false;
    int tier = VoHoc_GetAgentSetTier(casterId, VH_SET_BAN_THACH_BAT_CUC_QUYEN);

    /* Tương Sinh: Hỏa (2) -> Thổ (3) (Nung Thạch): target has Fire mark or Burn -> +1 Chấn Ấn */
    int bonus = 0;
    if (Entity_GetMarkStacks(targetId, 2) > 0) {
        Entity_ConsumeMarks(targetId, 2);
        bonus = 1;
    } else if (Entity_HasStatus(targetId, STATUS_BURN)) {
        bonus = 1;
    }

    int total = Entity_AddMark(targetId, 3, stacks + bonus, 3, 4.0f);
    if (total >= 3) {
        Entity_ConsumeMarks(targetId, 3);
        /* 0.75s airtime: v_y = 0.5 * g * t = 0.5 * 9.81 * 0.75 ~= 3.68 m/s */
        Entity_ApplyLaunch(targetId, 3.68f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_SetMarkImmune(targetId, 3, 3.0f);

        if (tier >= VO_HOC_TIER_MINOR_COUNT && casterId >= 0 && casterId < MAX_AGENTS) {
            double now = GetTime();
            if (s_tuThachLastTime[casterId] <= 0.0 || (now - s_tuThachLastTime[casterId]) >= 6.0) {
                s_tuThachLastTime[casterId] = now;
                Entity_AddShield(casterId, 14.0f, 3.0f);
                Entity_RestoreMana(casterId, 6.0f);
            }
        }
        return true;
    }
    return false;
}

bool VoHoc_OnBangSonConsume(int casterId, int targetId, int bangSonSkillIndex,
                            Vector3 impactPos, float *outMult) {
    int consumed = Entity_ConsumeMarks(targetId, 3);
    if (outMult) {
        *outMult = 1.0f + 0.12f * (float)consumed;
    }
    if (consumed >= 3 && VoHoc_GetAgentSetTier(casterId, VH_SET_BAN_THACH_BAT_CUC_QUYEN) >= VO_HOC_TIER_MAJOR_COUNT) {
        if (casterId >= 0 && casterId < MAX_AGENTS) {
            double now = GetTime();
            if (s_batDongLastTime[casterId] <= 0.0 || (now - s_batDongLastTime[casterId]) >= 10.0) {
                s_batDongLastTime[casterId] = now;
                float baseCd = Skill_GetCooldown(bangSonSkillIndex);
                SkillManager_TriggerCooldown(bangSonSkillIndex, casterId, baseCd * 0.60f);
            }
        }
        Entity_ApplyAoEDamageFrom(casterId, impactPos, 2.2f, 8.0f, 2.5f);
        return true;
    }
    return false;
}

bool VoHoc_ApplyMetalMark(int casterId, int targetId, int stacks) {
    if (stacks <= 0) return false;
    int tier = VoHoc_GetAgentSetTier(casterId, VH_SET_THIEN_CANG_DOAN_KIM_KIEM);

    /* Tương Sinh: Thổ (3) -> Kim (4): if target has Earth mark, consume it for +1 Tử Kim Ấn & +5 Mana */
    int bonus = 0;
    if (Entity_GetMarkStacks(targetId, 3) > 0) {
        Entity_ConsumeMarks(targetId, 3);
        Entity_RestoreMana(casterId, 5.0f);
        bonus = 1;
    }

    int total = Entity_AddMark(targetId, 4, stacks + bonus, 3, 4.5f);
    if (total >= 3) {
        Entity_ConsumeMarks(targetId, 4);
        Entity_ApplySilence(targetId, 0.85f);
        Entity_ApplyStatus(targetId, STATUS_VULNERABLE, 0.15f, 2.0f, casterId);
        Entity_SetMarkImmune(targetId, 4, 3.0f);

        const Agent *tgt = Entity_GetAgent(targetId);
        if (tgt) {
            VFX_ComposeContactSparkMode(tgt->position, VC_MAT_METAL, 0.85f, 0.90f, CONTACT_SPARK_CENTRIFUGAL);
        }

        if (tier >= VO_HOC_TIER_MINOR_COUNT) {
            /* Tiểu Thành: Kiếm Minh Dư Âm — +8 Mana and spread 1 Tử Kim Ấn to up to 2 enemies in 2.5m */
            Entity_RestoreMana(casterId, 8.0f);
            if (tgt) {
                int nearby[12];
                int cnt = Entity_GetNearbyTargets(tgt->position, 2.5f, nearby, 12);
                int spread = 0;
                for (int i = 0; i < cnt && spread < 2; i++) {
                    int nid = nearby[i];
                    if (nid == targetId || nid == casterId || !Entity_CanDamageTarget(casterId, nid)) continue;
                    Entity_AddMark(nid, 4, 1, 3, 4.5f);
                    spread++;
                }
            }
        }
        return true;
    }
    return false;
}

bool VoHoc_OnNhatKiemConsume(int casterId, int targetId, int nhatKiemSkillIndex,
                             int luuTinhSkillIndex, float *outMult) {
    int consumed = Entity_ConsumeMarks(targetId, 4);
    if (consumed > 3) consumed = 3;
    if (outMult) {
        *outMult = 1.0f + 0.10f * (float)consumed;
    }
    if (consumed >= 3 && VoHoc_GetAgentSetTier(casterId, VH_SET_THIEN_CANG_DOAN_KIM_KIEM) >= VO_HOC_TIER_MAJOR_COUNT) {
        if (casterId >= 0 && casterId < MAX_AGENTS) {
            double now = GetTime();
            if (s_nhatKiemResetLastTime[casterId] <= 0.0 || (now - s_nhatKiemResetLastTime[casterId]) >= 10.0) {
                s_nhatKiemResetLastTime[casterId] = now;
                float baseCd = Skill_GetCooldown(nhatKiemSkillIndex);
                SkillManager_TriggerCooldown(nhatKiemSkillIndex, casterId, baseCd * 0.55f);
                if (luuTinhSkillIndex >= 0) {
                    SkillManager_TriggerCooldown(luuTinhSkillIndex, casterId, 0.0f);
                }
                return true;
            }
        }
    }
    return false;
}
