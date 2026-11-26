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

/* Liệt Hỏa Phần Thiên Cước: adds `stacks` of Hỏa Chước Ấn (element 2, duration 4.5s).
 * - Tương Sinh (Mộc -> Hỏa): if target has Wood mark (1) or STATUS_POISON, consumes
 *   Wood mark (if present) for +1 Hỏa Chước Ấn and restores 5 Mana to caster.
 * - At 3 stacks (Nhiệt Bạo): triggers 12.5 HP Burn over 3.0s (~10.6 UL), clears stacks,
 *   sets 3.0s Kháng Hỏa Chước, spawns Shader-Puff ember detonation.
 * - Tiểu Thành (>= 4 pieces) — Dư Hỏa Phản Lực: on 3-stack trigger, refunds 8 Mana
 *   and grants +20% Haste for 2.0s (6s ICD).
 * Returns true if Nhiệt Bạo triggered. */
bool VoHoc_ApplyFireMark(int casterId, int targetId, int stacks);

/* Phần Thiên Toàn Phong Cước finisher hook: consumes all Hỏa Chước Ấn on target.
 * - Base: +12% damage multiplier per stack consumed (max +36%).
 * - Đại Thành (7/7 pieces) — Phần Thiên Liên Hoàn Cước: if >= 3 stacks consumed,
 *   reduces Toàn Phong cooldown by 45% (10s ICD) and resets Xích Diễm Trắc Cước cooldown.
 * Writes damage multiplier to *outMult if non-NULL; returns true if Đại Thành triggered. */
bool VoHoc_OnPhanThienConsume(int casterId, int targetId, int phanThienSkillIndex,
                              int xichDiemSkillIndex, float *outMult);

/* Giáng Long Thập Bát Chưởng: adds `stacks` of Long Ấn (element 2, duration 4.5s).
 * - At 3 stacks (Long Uy Bạo Phát): triggers Burn + Dragon Slam ember burst.
 * - Tiểu Thành (>= 4 pieces): refunds 8 Mana and reduces cooldowns by 0.8s (6s ICD). */
bool VoHoc_ApplyDragonMark(int casterId, int targetId, int stacks);

/* Kháng Long Hữu Hối finisher hook: consumes all Long Ấn on target.
 * - Base: +12% damage multiplier per stack consumed (max +36%).
 * - Đại Thành (7/7 pieces): if >= 3 stacks consumed, halves Kháng Long cooldown (10s ICD)
 *   and resets Tiềm Long Vật Dụng cooldown. */
bool VoHoc_OnKhangLongConsume(int casterId, int targetId, int khangLongSkillIndex,
                              int tiemLongSkillIndex, float *outMult);

#ifdef __cplusplus
}
#endif

#endif /* SKILLS_VO_HOC_SETS_H */
