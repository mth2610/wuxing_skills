// open_world/realms/ow_wuxing_realms.c
// Data-driven Open-World configurations for Mau Son (Thuy), Ly Son (Hoa), and Phu Quoc (Kim).
#include "open_world/realms/ow_wuxing_realms.h"

static const OWRealmDef kMauSonWaterRealm = {
    .realmId         = 0,
    .scaleWithMap    = true,
    .safeCenter      = { 135.0f, 0.0f, 112.5f },
    .safeRadius      = 18.0f,
    .daTauNpcPos     = { 131.5f, 0.0f, 108.5f },
    .alchemistPos    = { 138.5f, 0.0f, 109.5f },
    .stelePos        = { 129.0f, 0.0f, 116.5f },
    .altarPos        = { 164.0f, 0.0f, 136.0f },
    .duelBannerPos   = { 149.0f, 0.0f, 124.0f },
    .defaultStartPos = { 135.0f, 0.0f, 112.5f },
    .dunesCenter     = { 164.0f, 0.0f, 136.0f },
    .canyonCenter    = { 105.0f, 0.0f, 148.0f },
    .badlandsCenter  = { 172.0f, 0.0f, 76.0f  },
    .zoneCount = 4,
    .zones = {
        { { 135.0f, 0.0f, 112.5f }, 18.0f, "Hàn Đàm Mẫu Sơn [An Toàn]" },
        { { 164.0f, 0.0f, 136.0f }, 28.0f, "Triền Băng Tuyết [Tân Thủ]" },
        { { 105.0f, 0.0f, 148.0f }, 36.0f, "Hồ Thiên Sơn [Giang Hồ]" },
        { { 172.0f, 0.0f, 76.0f  }, 38.0f, "Đỉnh Băng Phong [Hung Hiểm]" }
    },
    .nodeCount = 10,
    .nodes = {
        { OW_NODE_HERB, 142.0f, 118.0f, true },
        { OW_NODE_HERB, 126.0f, 106.0f, true },
        { OW_NODE_HERB, 158.0f, 132.0f, true },
        { OW_NODE_HERB, 112.0f, 144.0f, true },
        { OW_NODE_ORE_VEIN, 168.0f, 142.0f, true },
        { OW_NODE_ORE_VEIN, 102.0f, 154.0f, true },
        { OW_NODE_ORE_VEIN, 166.0f, 84.0f,  true },
        { OW_NODE_BURIED_CHEST, 116.0f, 152.0f, true },
        { OW_NODE_BURIED_CHEST, 160.0f, 92.0f,  true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 178.0f, 72.0f, true }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 150.0f, 0.0f, 124.0f },
        { 120.0f, 0.0f, 132.0f },
        { 156.0f, 0.0f, 96.0f  },
        { 170.0f, 0.0f, 78.0f  }
    },
    .campCount = 4,
    .camps = {
        { { 164.0f, 0.0f, 136.0f }, 5, 0, MONSTER_SWARM_MELEE },
        { { 105.0f, 0.0f, 148.0f }, 4, 0, MONSTER_RANGED_CASTER },
        { { 158.0f, 0.0f, 92.0f  }, 5, 0, MONSTER_LEAPER },
        { { 172.0f, 0.0f, 76.0f  }, 2, 0, MONSTER_ELITE_GUARD }
    },
    .hordeZoneCount = 6,
    .hordeZones = {
        { { 164.0f, 0.0f, 136.0f }, 18.0f, 0, 22, 48, 4.4f, 10, 0.16f, 0.10f, 0 },
        { { 105.0f, 0.0f, 148.0f }, 22.0f, 0, 28, 60, 3.9f, 12, 0.22f, 0.16f, 1 },
        { { 184.0f, 0.0f, 146.0f }, 20.0f, 0, 26, 56, 4.0f, 12, 0.20f, 0.18f, 1 },
        { { 158.0f, 0.0f, 92.0f  }, 22.0f, 0, 30, 64, 3.6f, 14, 0.25f, 0.15f, 1 },
        { { 172.0f, 0.0f, 76.0f  }, 24.0f, 0, 34, 72, 3.4f, 16, 0.28f, 0.18f, 2 },
        { { 96.0f,  0.0f, 98.0f  }, 20.0f, 0, 24, 52, 4.2f, 10, 0.18f, 0.14f, 1 }
    },
    .weatherProfileCount = 2,
    .weatherProfiles = {
        {
            .type = OW_WEATHER_CLEAR, .name = "Sương Tuyết Mẫu Sơn",
            .startBanner = "HÀN KHÍ MẪU SƠN — BĂNG TUYẾT TĨNH LẶNG",
            .defaultDurationSec = 70.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { -0.82f, 0.57f }, .windSpeedMps = 3.8f, .windTurbulence = 0.28f,
            .accentColor = { 175, 225, 255, 255 }, .xpMultiplier = 1.0f
        },
        {
            .type = OW_WEATHER_SPIRIT_PHENOM, .name = "Băng Linh Cuồng Phong",
            .startBanner = "HÀN THỦY LINH KHÍ! x1.5 EXP VÕ HỌC & +15% TỐC CHẠY!",
            .defaultDurationSec = 50.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { -0.70f, 0.70f }, .windSpeedMps = 7.2f, .windTurbulence = 0.48f,
            .accentColor = { 135, 240, 255, 255 }, .moveSpeedBonus = 0.15f, .xpMultiplier = 1.50f,
            .spawnSpiritOrbs = true
        }
    },
    .spawnerPresetCount = 4,
    .spawnerPresets = {
        { "NHA BAY THUY YEU (36 QUAI)", "TE DAN NHA BAY THUY YEU!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 36, 0, 0 },
        { "TRIEU HOI BANG YEU (80 QUAI)", "80 QUAI TRAN RA KHAP MAU SON!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 80, 0, 0 },
        { "GOI BOSS HAN THUY TON GIA", "HAN THUY TON GIA THUC TINH!", OW_SPAWN_WORLD_BOSS, MONSTER_ELITE_GUARD, 1, 0, 1 },
        { "MOI CAO THU TY THI (1v1)", "CAO THU GIANG HO NHAP LOI DAI!", OW_SPAWN_AI_RIVAL_SPAR, MONSTER_SWARM_MELEE, 1, 0, 0 }
    },
    .pvpConfig = {
        .sparRingRadius = 12.0f, .sparRewardGold = 60, .sparRewardSkillXp = 60,
        .slaughterDmgBonus = 0.08f, .slaughterSpeedBonus = 0.15f, .bountyInfamyThreshold = 2
    }
};

static const OWRealmDef kLySonFireRealm = {
    .realmId         = 2,
    .scaleWithMap    = true,
    .safeCenter      = { 144.0f, 0.0f, 105.0f },
    .safeRadius      = 18.0f,
    .daTauNpcPos     = { 140.5f, 0.0f, 101.0f },
    .alchemistPos    = { 147.5f, 0.0f, 102.0f },
    .stelePos        = { 138.0f, 0.0f, 109.0f },
    .altarPos        = { 172.0f, 0.0f, 128.0f },
    .duelBannerPos   = { 158.0f, 0.0f, 116.0f },
    .defaultStartPos = { 144.0f, 0.0f, 105.0f },
    .dunesCenter     = { 172.0f, 0.0f, 128.0f },
    .canyonCenter    = { 112.0f, 0.0f, 136.0f },
    .badlandsCenter  = { 182.0f, 0.0f, 74.0f  },
    .zoneCount = 4,
    .zones = {
        { { 144.0f, 0.0f, 105.0f }, 18.0f, "Đỉnh Thới Lới [An Toàn]" },
        { { 172.0f, 0.0f, 128.0f }, 28.0f, "Triền Đá Bazan [Tân Thủ]" },
        { { 112.0f, 0.0f, 136.0f }, 36.0f, "Miệng Núi Lửa [Giang Hồ]" },
        { { 182.0f, 0.0f, 74.0f  }, 38.0f, "Huyết Diệm Dung Nham [Hung Hiểm]" }
    },
    .nodeCount = 10,
    .nodes = {
        { OW_NODE_HERB, 150.0f, 111.0f, true },
        { OW_NODE_HERB, 136.0f, 99.0f,  true },
        { OW_NODE_HERB, 166.0f, 122.0f, true },
        { OW_NODE_HERB, 118.0f, 132.0f, true },
        { OW_NODE_ORE_VEIN, 176.0f, 134.0f, true },
        { OW_NODE_ORE_VEIN, 108.0f, 142.0f, true },
        { OW_NODE_ORE_VEIN, 174.0f, 82.0f,  true },
        { OW_NODE_BURIED_CHEST, 122.0f, 140.0f, true },
        { OW_NODE_BURIED_CHEST, 168.0f, 88.0f,  true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 186.0f, 70.0f, true }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 158.0f, 0.0f, 116.0f },
        { 128.0f, 0.0f, 124.0f },
        { 164.0f, 0.0f, 92.0f  },
        { 178.0f, 0.0f, 76.0f  }
    },
    .campCount = 4,
    .camps = {
        { { 172.0f, 0.0f, 128.0f }, 5, 2, MONSTER_SWARM_MELEE },
        { { 112.0f, 0.0f, 136.0f }, 4, 2, MONSTER_RANGED_CASTER },
        { { 164.0f, 0.0f, 88.0f  }, 5, 2, MONSTER_LEAPER },
        { { 182.0f, 0.0f, 74.0f  }, 2, 2, MONSTER_ELITE_GUARD }
    },
    .hordeZoneCount = 6,
    .hordeZones = {
        { { 172.0f, 0.0f, 128.0f }, 18.0f, 2, 22, 48, 4.4f, 10, 0.16f, 0.10f, 0 },
        { { 112.0f, 0.0f, 136.0f }, 22.0f, 2, 28, 60, 3.9f, 12, 0.22f, 0.16f, 1 },
        { { 190.0f, 0.0f, 138.0f }, 20.0f, 2, 26, 56, 4.0f, 12, 0.20f, 0.18f, 1 },
        { { 164.0f, 0.0f, 88.0f  }, 22.0f, 2, 30, 64, 3.6f, 14, 0.25f, 0.15f, 1 },
        { { 182.0f, 0.0f, 74.0f  }, 24.0f, 2, 34, 72, 3.4f, 16, 0.28f, 0.18f, 2 },
        { { 102.0f, 0.0f, 92.0f  }, 20.0f, 2, 24, 52, 4.2f, 10, 0.18f, 0.14f, 1 }
    },
    .weatherProfileCount = 2,
    .weatherProfiles = {
        {
            .type = OW_WEATHER_CLEAR, .name = "Nắng Gắt Hỏa Sơn",
            .startBanner = "LÝ SƠN HỎA SƠN — DUNG NHAM ÂM Ỉ",
            .defaultDurationSec = 70.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.85f, 0.52f }, .windSpeedMps = 4.0f, .windTurbulence = 0.30f,
            .accentColor = { 255, 148, 72, 255 }, .xpMultiplier = 1.0f
        },
        {
            .type = OW_WEATHER_SPIRIT_PHENOM, .name = "Hỏa Diệm Bão Tro",
            .startBanner = "HỎA LINH BỘC PHÁT! x1.5 EXP VÕ HỌC & +15% TỐC CHẠY!",
            .defaultDurationSec = 50.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.72f, 0.68f }, .windSpeedMps = 7.4f, .windTurbulence = 0.50f,
            .accentColor = { 255, 112, 48, 255 }, .moveSpeedBonus = 0.15f, .xpMultiplier = 1.50f,
            .spawnSpiritOrbs = true
        }
    },
    .spawnerPresetCount = 4,
    .spawnerPresets = {
        { "NHA BAY HOA YEU (36 QUAI)", "TE DAN NHA BAY HOA YEU!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 36, 2, 0 },
        { "TRIEU HOI DUNG NHAM (80 QUAI)", "80 QUAI TRAN RA KHAP HOA SON!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 80, 2, 0 },
        { "GOI BOSS XICH HOA MA TON", "XICH HOA MA TON THUC TINH!", OW_SPAWN_WORLD_BOSS, MONSTER_ELITE_GUARD, 1, 2, 1 },
        { "MOI CAO THU TY THI (1v1)", "CAO THU GIANG HO NHAP LOI DAI!", OW_SPAWN_AI_RIVAL_SPAR, MONSTER_SWARM_MELEE, 1, 2, 0 }
    },
    .pvpConfig = {
        .sparRingRadius = 12.0f, .sparRewardGold = 60, .sparRewardSkillXp = 60,
        .slaughterDmgBonus = 0.08f, .slaughterSpeedBonus = 0.15f, .bountyInfamyThreshold = 2
    }
};

static const OWRealmDef kPhuQuocMetalRealm = {
    .realmId         = 4,
    .scaleWithMap    = true,
    .safeCenter      = { 101.0f, 0.0f, 144.0f },
    .safeRadius      = 18.0f,
    .daTauNpcPos     = { 97.5f,  0.0f, 140.0f },
    .alchemistPos    = { 104.5f, 0.0f, 141.0f },
    .stelePos        = { 95.0f,  0.0f, 148.0f },
    .altarPos        = { 126.0f, 0.0f, 170.0f },
    .duelBannerPos   = { 114.0f, 0.0f, 156.0f },
    .defaultStartPos = { 101.0f, 0.0f, 144.0f },
    .dunesCenter     = { 126.0f, 0.0f, 170.0f },
    .canyonCenter    = { 76.0f,  0.0f, 188.0f },
    .badlandsCenter  = { 132.0f, 0.0f, 92.0f  },
    .zoneCount = 4,
    .zones = {
        { { 101.0f, 0.0f, 144.0f }, 18.0f, "Suối Tranh Kim Cốc [An Toàn]" },
        { { 126.0f, 0.0f, 170.0f }, 28.0f, "Bãi Quặng Hoàng Kim [Tân Thủ]" },
        { { 76.0f,  0.0f, 188.0f }, 36.0f, "Kim Tinh Đàm [Giang Hồ]" },
        { { 132.0f, 0.0f, 92.0f  }, 38.0f, "Thạch Mạch Bạch Kim [Hung Hiểm]" }
    },
    .nodeCount = 10,
    .nodes = {
        { OW_NODE_HERB, 108.0f, 150.0f, true },
        { OW_NODE_HERB, 92.0f,  138.0f, true },
        { OW_NODE_HERB, 120.0f, 166.0f, true },
        { OW_NODE_HERB, 82.0f,  182.0f, true },
        { OW_NODE_ORE_VEIN, 130.0f, 174.0f, true },
        { OW_NODE_ORE_VEIN, 72.0f,  194.0f, true },
        { OW_NODE_ORE_VEIN, 124.0f, 100.0f, true },
        { OW_NODE_BURIED_CHEST, 84.0f,  190.0f, true },
        { OW_NODE_BURIED_CHEST, 120.0f, 106.0f, true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 138.0f, 84.0f, true }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 114.0f, 0.0f, 158.0f },
        { 88.0f,  0.0f, 168.0f },
        { 118.0f, 0.0f, 114.0f },
        { 130.0f, 0.0f, 94.0f  }
    },
    .campCount = 4,
    .camps = {
        { { 126.0f, 0.0f, 170.0f }, 5, 4, MONSTER_SWARM_MELEE },
        { { 76.0f,  0.0f, 188.0f }, 4, 4, MONSTER_RANGED_CASTER },
        { { 118.0f, 0.0f, 108.0f }, 5, 4, MONSTER_LEAPER },
        { { 132.0f, 0.0f, 92.0f  }, 2, 4, MONSTER_ELITE_GUARD }
    },
    .hordeZoneCount = 6,
    .hordeZones = {
        { { 126.0f, 0.0f, 170.0f }, 18.0f, 4, 22, 48, 4.4f, 10, 0.16f, 0.10f, 0 },
        { { 76.0f,  0.0f, 188.0f }, 22.0f, 4, 28, 60, 3.9f, 12, 0.22f, 0.16f, 1 },
        { { 146.0f, 0.0f, 198.0f }, 20.0f, 4, 26, 56, 4.0f, 12, 0.20f, 0.18f, 1 },
        { { 118.0f, 0.0f, 108.0f }, 22.0f, 4, 30, 64, 3.6f, 14, 0.25f, 0.15f, 1 },
        { { 132.0f, 0.0f, 92.0f  }, 24.0f, 4, 34, 72, 3.4f, 16, 0.28f, 0.18f, 2 },
        { { 64.0f,  0.0f, 120.0f }, 20.0f, 4, 24, 52, 4.2f, 10, 0.18f, 0.14f, 1 }
    },
    .weatherProfileCount = 2,
    .weatherProfiles = {
        {
            .type = OW_WEATHER_CLEAR, .name = "Gió Biển Kim Cốc",
            .startBanner = "PHÚ QUỐC KIM CỐC — SUỐI VÀNG LẤP LÁNH",
            .defaultDurationSec = 70.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.86f, -0.50f }, .windSpeedMps = 3.6f, .windTurbulence = 0.26f,
            .accentColor = { 255, 228, 138, 255 }, .xpMultiplier = 1.0f
        },
        {
            .type = OW_WEATHER_SPIRIT_PHENOM, .name = "Kim Khí Quang Vũ",
            .startBanner = "KIM LINH TỤ MẠCH! x1.5 EXP VÕ HỌC & +15% TỐC CHẠY!",
            .defaultDurationSec = 50.0f, .atmoMode = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection = { 0.78f, -0.62f }, .windSpeedMps = 6.8f, .windTurbulence = 0.44f,
            .accentColor = { 255, 240, 175, 255 }, .moveSpeedBonus = 0.15f, .xpMultiplier = 1.50f,
            .spawnSpiritOrbs = true
        }
    },
    .spawnerPresetCount = 4,
    .spawnerPresets = {
        { "NHA BAY KIM YEU (36 QUAI)", "TE DAN NHA BAY KIM YEU!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 36, 4, 0 },
        { "TRIEU HOI QUANG YEU (80 QUAI)", "80 QUAI TRAN RA KHAP KIM COC!", OW_SPAWN_MONSTER_PACK, MONSTER_SWARM_MELEE, 80, 4, 0 },
        { "GOI BOSS BACH KIM TON GIA", "BACH KIM TON GIA THUC TINH!", OW_SPAWN_WORLD_BOSS, MONSTER_ELITE_GUARD, 1, 4, 1 },
        { "MOI CAO THU TY THI (1v1)", "CAO THU GIANG HO NHAP LOI DAI!", OW_SPAWN_AI_RIVAL_SPAR, MONSTER_SWARM_MELEE, 1, 4, 0 }
    },
    .pvpConfig = {
        .sparRingRadius = 12.0f, .sparRewardGold = 60, .sparRewardSkillXp = 60,
        .slaughterDmgBonus = 0.08f, .slaughterSpeedBonus = 0.15f, .bountyInfamyThreshold = 2
    }
};

const OWRealmDef *OWRealm_GetMauSonWater(void) { return &kMauSonWaterRealm; }
const OWRealmDef *OWRealm_GetLySonFire(void)   { return &kLySonFireRealm; }
const OWRealmDef *OWRealm_GetPhuQuocMetal(void) { return &kPhuQuocMetalRealm; }
