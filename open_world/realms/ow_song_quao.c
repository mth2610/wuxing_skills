// open_world/realms/ow_song_quao.c
// Data-driven Open-World configuration for Song Quao Valley (Thuy / Moc Realms).
#include "open_world/realms/ow_song_quao.h"

static const OWRealmDef kSongQuaoWaterRealm = {
    .realmId         = 0,
    .scaleWithMap    = true,
    .safeCenter      = { 94.15f, 0.0f, 150.0f },
    .safeRadius      = 16.0f,
    .daTauNpcPos     = { 91.0f,  0.0f, 146.0f },
    .alchemistPos    = { 97.5f,  0.0f, 147.0f },
    .stelePos        = { 88.5f,  0.0f, 154.0f },
    .altarPos        = { 118.0f, 0.0f, 175.0f },
    .duelBannerPos   = { 106.0f, 0.0f, 162.0f },
    .defaultStartPos = { 94.15f, 0.0f, 150.0f },
    .dunesCenter     = { 118.0f, 0.0f, 175.0f },
    .canyonCenter    = { 148.0f, 0.0f, 212.0f },
    .badlandsCenter  = { 110.0f, 0.0f, 92.0f  },
    .zoneCount = 4,
    .zones = {
        { { 94.15f, 0.0f, 150.0f }, 16.0f, "Đỉnh Thảo Nguyên [An Toàn]" },
        { { 118.0f, 0.0f, 175.0f }, 26.0f, "Đồi Cỏ Xanh [Tân Thủ]" },
        { { 148.0f, 0.0f, 212.0f }, 34.0f, "Hồ Sông Quao [Giang Hồ]" },
        { { 110.0f, 0.0f, 92.0f  }, 36.0f, "Thung Lũng Thượng Nguồn [Hung Hiểm]" }
    },
    .nodeCount = 10,
    .nodes = {
        { OW_NODE_HERB, 102.0f, 156.0f, true },
        { OW_NODE_HERB, 86.0f,  144.0f, true },
        { OW_NODE_HERB, 134.0f, 196.0f, true },
        { OW_NODE_HERB, 158.0f, 224.0f, true },
        { OW_NODE_ORE_VEIN, 122.0f, 166.0f, true },
        { OW_NODE_ORE_VEIN, 108.0f, 104.0f, true },
        { OW_NODE_ORE_VEIN, 95.0f,  84.0f,  true },
        { OW_NODE_BURIED_CHEST, 142.0f, 205.0f, true },
        { OW_NODE_BURIED_CHEST, 114.0f, 96.0f,  true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 126.0f, 64.0f, true }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 108.0f, 0.0f, 165.0f },
        { 138.0f, 0.0f, 196.0f },
        { 106.0f, 0.0f, 122.0f },
        { 118.0f, 0.0f, 78.0f  }
    },
    .campCount = 4,
    .camps = {
        { { 118.0f, 0.0f, 176.0f }, 5, 0, MONSTER_SWARM_MELEE },
        { { 148.0f, 0.0f, 212.0f }, 4, 0, MONSTER_RANGED_CASTER },
        { { 108.0f, 0.0f, 108.0f }, 5, 0, MONSTER_LEAPER },
        { { 114.0f, 0.0f, 82.0f  }, 2, 0, MONSTER_ELITE_GUARD }
    },
    .hordeZoneCount = 6,
    .hordeZones = {
        { { 118.0f, 0.0f, 176.0f }, 18.0f, 0, 22, 48, 4.4f, 10, 0.16f, 0.10f, 0 },
        { { 144.0f, 0.0f, 204.0f }, 22.0f, 0, 28, 60, 3.9f, 12, 0.22f, 0.16f, 1 },
        { { 158.0f, 0.0f, 228.0f }, 20.0f, 0, 26, 56, 4.0f, 12, 0.20f, 0.18f, 1 },
        { { 108.0f, 0.0f, 112.0f }, 22.0f, 0, 30, 64, 3.6f, 14, 0.25f, 0.15f, 1 },
        { { 112.0f, 0.0f, 82.0f  }, 24.0f, 0, 34, 72, 3.4f, 16, 0.28f, 0.18f, 2 },
        { { 72.0f,  0.0f, 185.0f }, 20.0f, 0, 24, 52, 4.2f, 10, 0.18f, 0.14f, 1 }
    },
    .weatherProfileCount = 2,
    .weatherProfiles = {
        {
            .type = OW_WEATHER_CLEAR, .name = "Nắng Sớm Thảo Nguyên",
            .startBanner = "GIÓ THUNG LŨNG THỔI NHẸ — DẠO CẢNH & LUYỆN VÕ",
            .defaultDurationSec = 70.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.80f, 0.60f }, .windSpeedMps = 3.2f, .windTurbulence = 0.25f,
            .accentColor = { 135, 225, 245, 255 }, .xpMultiplier = 1.0f
        },
        {
            .type = OW_WEATHER_SPIRIT_PHENOM, .name = "Linh Khí Sông Quao",
            .startBanner = "LINH KHÍ TỤ HỘI! x1.5 EXP VÕ HỌC & +15% TỐC CHẠY!",
            .defaultDurationSec = 50.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.70f, 0.70f }, .windSpeedMps = 6.5f, .windTurbulence = 0.45f,
            .accentColor = { 115, 245, 210, 255 }, .moveSpeedBonus = 0.15f, .xpMultiplier = 1.50f,
            .spawnSpiritOrbs = true
        }
    },
    .spawnerPresetCount = 4,
    .spawnerPresets = {
        { "NHA BAY THUY YEU (36 QUAI)", "TE DAN NHA BAY THUY YEU!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 36, 0, 0 },
        { "TRIEU HOI SONG YEU (80 QUAI)", "80 QUAI TRAN RA KHAP THAO NGUYEN!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 80, 0, 0 },
        { "GOI BOSS MOC LINH CO THU", "MOC LINH CO THU THUC TINH!", OW_SPAWN_WORLD_BOSS, MONSTER_ELITE_GUARD, 1, 1, 1 },
        { "MOI CAO THU TY THI (1v1)", "CAO THU GIANG HO NHAP LOI DAI!", OW_SPAWN_AI_RIVAL_SPAR, MONSTER_SWARM_MELEE, 1, 0, 0 }
    },
    .pvpConfig = {
        .sparRingRadius = 12.0f, .sparRewardGold = 60, .sparRewardSkillXp = 60,
        .slaughterDmgBonus = 0.08f, .slaughterSpeedBonus = 0.15f, .bountyInfamyThreshold = 2
    }
};

static OWRealmDef s_woodRealmCache = { 0 };

const OWRealmDef *OWRealm_GetSongQuao(int realmId) {
    if (realmId == 1) {
        s_woodRealmCache = kSongQuaoWaterRealm;
        s_woodRealmCache.realmId = 1;
        for (int i = 0; i < s_woodRealmCache.campCount; i++) s_woodRealmCache.camps[i].element = 1;
        for (int i = 0; i < s_woodRealmCache.hordeZoneCount; i++) s_woodRealmCache.hordeZones[i].element = 1;
        return &s_woodRealmCache;
    }
    return &kSongQuaoWaterRealm;
}
