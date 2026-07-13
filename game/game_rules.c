// game/game_rules.c — the static zone modifier table. Keep every rule here;
// consumers get plain multipliers/flags, never re-derive the table.
#include "game/game_rules.h"
#include <stddef.h> // NULL

enum { E_WATER = 0, E_WOOD = 1, E_FIRE = 2, E_EARTH = 3, E_METAL = 4 };

float GameRules_CooldownMult(int element, NatureZoneType zone) {
    if (zone == NAT_RIVER && element == E_WATER) return 0.5f; // Thủy đắc thủy
    return 1.0f;
}

float GameRules_DamageMult(int element, NatureZoneType zone) {
    if (zone == NAT_RIVER && element == E_FIRE) return 0.5f;   // Hỏa gặp nước
    if (zone == NAT_FOREST && element == E_EARTH) return 0.5f; // đạn Thổ vào Rừng (combat/ enforces)
    return 1.0f;
}

bool GameRules_GrantsStealth(int element, NatureZoneType zone) {
    return zone == NAT_FOREST && element == E_WOOD; // Mộc ẩn mình trong Rừng
}

float GameRules_KnockbackMult(int element, NatureZoneType zone) {
    if (zone == NAT_DESERT_ZONE && element == E_EARTH) return 1.5f; // Thổ đắc địa
    return 1.0f;
}

float GameRules_RangeMult(int element, NatureZoneType zone) {
    if (zone == NAT_DESERT_ZONE && element == E_WATER) return 0.5f; // Thủy khô cạn
    return 1.0f;
}

int GameRules_CountAliveHeroes(AgentTeam team) {
    int n = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a != NULL && a->archetype == ARCH_HERO && a->team == team) n++;
    }
    return n;
}

TeamHandicap GameRules_HandicapFor(int deficit) {
    if (deficit < 0) deficit = 0;
    if (deficit > 3) deficit = 3;
    // Khởi điểm — tinh chỉnh qua playtest (KE_HOACH Đợt A4/D4).
    return (TeamHandicap){ 1.0f + 0.15f * (float)deficit,
                           1.0f + 0.05f * (float)deficit };
}
