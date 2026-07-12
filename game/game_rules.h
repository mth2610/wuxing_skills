// game/game_rules.h
// Zone modifier rule table (MODULES_ROADMAP.md Module 2's luật, homed in
// game/ per Module 7 — "luật gameplay tập trung một chỗ"). Maps are pure
// data (WHERE the zones are); THIS is the single place that says what a
// zone does to which element. Pure functions, no state.
//
// | Zone            | Hệ hưởng lợi                 | Hệ chịu thiệt              |
// | NAT_RIVER       | Thủy: -50% cooldown          | Hỏa: -50% damage           |
// | NAT_FOREST      | Mộc: ẩn hình (stealth)       | Thổ: đạn -50% dmg (combat) |
// | NAT_DESERT_ZONE | Thổ: +50% knockback          | Thủy: -50% tầm đánh        |
//
// Element indices follow Agent.currentElement (0=Water..4=Metal). The Thổ-
// in-forest projectile penalty is enforced inside combat/ (it needs the
// projectile position); everything else is applied by game_screen to the
// player each frame via control/entities setters.
#ifndef GAME_RULES_H
#define GAME_RULES_H

#include "core/map_manager.h" // NatureZoneType
#include <stdbool.h>

float GameRules_CooldownMult(int element, NatureZoneType zone);  // 0.5 = twice as fast
float GameRules_DamageMult(int element, NatureZoneType zone);    // scales outgoing damage
bool  GameRules_GrantsStealth(int element, NatureZoneType zone);
float GameRules_KnockbackMult(int element, NatureZoneType zone);
float GameRules_RangeMult(int element, NatureZoneType zone);

#endif // GAME_RULES_H
