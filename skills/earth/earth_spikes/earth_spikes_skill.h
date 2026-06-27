/*
 * earth_spikes_skill.h  —  Địa Sa Châm (Earth Spikes)
 * Element : Earth  |  Category : SKILL_CAT_AOE_CONTROL
 * Range   : ~280 units (mid-range sequential spike line)
 *
 * Place in: skills/earth/earth_spikes/earth_spikes_skill.h
 * Auto-detected by the skill registration system via the
 * standard five-function lifecycle naming convention.
 *
 * Mechanic overview
 * -----------------
 *  • CastEarthSpikesSkill spawns a line of SPIKES_PER_CAST rock
 *    spikes from caster toward target.  Each spike erupts 0.08 s
 *    after the previous one, giving a "tremor racing across the
 *    ground" feel.
 *  • On emergence each spike deals AoE damage (r ≈ 30 u) and
 *    applies knockback + root (1 s) to any enemy in range.
 *  • Dust particles burst radially from the base; a crack decal
 *    is stamped into the ground; a warm VFX point-light flares.
 *  • When lifetime expires the spike is crumbled back into the
 *    earth by earth_spikes.fs (erosion/dissolve shader).
 */

#ifndef SKILL_EARTH_SPIKES_H
#define SKILL_EARTH_SPIKES_H

#include "raylib.h"
#include "core/skill_manager.h"   /* SkillParams, SkillCategory */

/* ── Standard lifecycle API (auto-registered by the engine) ─── */
void InitEarthSpikesSkill  (int screenWidth, int screenHeight);
void CastEarthSpikesSkill  (Vector3 startPos, Vector3 target, SkillParams params);
void UpdateEarthSpikesSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawEarthSpikesSkill  (void);
void UnloadEarthSpikesSkill(void);

#endif /* SKILL_EARTH_SPIKES_H */
