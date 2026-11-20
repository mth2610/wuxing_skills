#ifndef SKILLS_VO_HOC_SETS_H
#define SKILLS_VO_HOC_SETS_H

#include "skills/vo_hoc/vo_hoc_codex.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 (none), 4 (Tiểu Thành), or 7 (Đại Thành) for the agent's equipped loadout. */
int VoHoc_GetAgentSetTier(int agentId, VoHocSetId setId);

/* Thủy Triều Quyền Pháp: adds `stacks` of Triều Ấn (element 0).
 * - At 3 stacks: triggers Freeze (0.75s, subject to DR), clears stacks, sets 3s Kháng Triều.
 * - Tiểu Thành (>= 4 pieces): mark duration 4s -> 6s, refunds 10 Mana to caster on freeze.
 * Returns true if Freeze triggered. */
bool VoHoc_ApplyWaterMark(int casterId, int targetId, int stacks);

/* Nộ Hải Long Quyền finisher hook: consumes all Triều Ấn on target.
 * - Base: +12% damage multiplier per stack consumed.
 * - Đại Thành (7/7 pieces): if >= 3 stacks consumed, halves Nộ Hải cooldown (10s ICD)
 *   and adds +10 Shatter bonus damage.
 * Returns extra flat damage to add; writes damage multiplier to *outMult if non-NULL. */
float VoHoc_OnNoHaiConsume(int casterId, int targetId, int noHaiSkillIndex, float *outMult);

#ifdef __cplusplus
}
#endif

#endif /* SKILLS_VO_HOC_SETS_H */
