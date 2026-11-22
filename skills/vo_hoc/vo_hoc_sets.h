#ifndef SKILLS_VO_HOC_SETS_H
#define SKILLS_VO_HOC_SETS_H

#include "skills/vo_hoc/vo_hoc_codex.h"
#include "raylib.h"
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

/* Bàn Thạch Bát Cực Quyền: adds `stacks` of Chấn Ấn (element 3).
 * - At 3 stacks: triggers Địa Liệt Đảo Địa (Launch 0.75s, subject to DR), clears
 *   stacks, sets 3s Kháng Chấn.
 * - Tương Sinh (Hỏa -> Thổ): if target has Fire mark (2) or STATUS_BURN, +1 Chấn Ấn.
 * - Tiểu Thành (>= 4 pieces) — Tụ Thạch Thành Sơn: on 3-stack Launch trigger,
 *   grants caster 14 HP Shield (3.0s) and restores 6 Mana (6s ICD).
 * Returns true if Launch triggered. */
bool VoHoc_ApplyEarthMark(int casterId, int targetId, int stacks);

/* Băng Sơn Toái Chưởng finisher hook: consumes all Chấn Ấn on target.
 * - Base: +12% damage multiplier per stack consumed.
 * - Đại Thành (7/7 pieces) — Bất Động Như Sơn: if >= 3 stacks consumed, reduces
 *   Băng Sơn cooldown by 40% (10s ICD) and triggers aftershock (2.5 m/s KB + 8 dmg
 *   in 2.2m radius around impactPos).
 * Writes damage multiplier to *outMult if non-NULL; returns true if Đại Thành
 * aftershock triggered. */
bool VoHoc_OnBangSonConsume(int casterId, int targetId, int bangSonSkillIndex,
                            Vector3 impactPos, float *outMult);

/* Thiên Cang Đoạn Kim Kiếm Pháp: adds `stacks` of Tử Kim Ấn (element 4, duration 4.5s).
 * - Tương Sinh (Thổ -> Kim): if target has Earth mark (3), consumes it for +1 Tử Kim Ấn
 *   and restores 5 Mana to caster.
 * - At 3 stacks (Cộng Hưởng Phá Khí): triggers Silence (0.85s) + Vulnerable (+15%, 2.0s),
 *   clears stacks, sets 3.0s Kháng Cộng Hưởng.
 * - Tiểu Thành (>= 4 pieces) — Kiếm Minh Dư Âm: on 3-stack trigger, propagates +1 Tử Kim Ấn
 *   to up to 2 nearby enemies (r=2.5m) and refunds 8 Mana to caster.
 * Returns true if Cộng Hưởng Phá Khí triggered. */
bool VoHoc_ApplyMetalMark(int casterId, int targetId, int stacks);

/* Nhất Kiếm Đoạn Thiên finisher hook: consumes all Tử Kim Ấn on target.
 * - Base: +10% damage multiplier per stack consumed (max +30%).
 * - Đại Thành (7/7 pieces) — Vạn Kiếm Quy Tông: if >= 3 stacks consumed, reduces
 *   Nhất Kiếm cooldown by 45% (10s ICD) and immediately resets Lưu Tinh Thứ cooldown.
 * Writes damage multiplier to *outMult if non-NULL; returns true if Đại Thành triggered. */
bool VoHoc_OnNhatKiemConsume(int casterId, int targetId, int nhatKiemSkillIndex,
                             int luuTinhSkillIndex, float *outMult);

#ifdef __cplusplus
}
#endif

#endif /* SKILLS_VO_HOC_SETS_H */
