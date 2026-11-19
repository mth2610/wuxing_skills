// open_world/realms/ow_desert_earth.c
// Data-driven configuration for Realm 3: Hoàng Thổ Đại Mạc (NINH_THUAN — Earth Element).
// Zero engine logic here: tune weather, spawner presets, zones, and PvP rules purely via data.
#include "open_world/realms/ow_desert_earth.h"

static const OWRealmDef kDesertEarthRealm = {
    .realmId         = 3,
    .scaleWithMap    = true,
    .safeCenter      = { 170.68f, 0.0f, 230.41f },
    .safeRadius      = 22.0f,
    .daTauNpcPos     = { 163.5f,  0.0f, 224.0f  },
    .alchemistPos    = { 168.0f,  0.0f, 226.5f  },
    .stelePos        = { 157.5f,  0.0f, 219.0f  },
    .altarPos        = { 132.0f,  0.0f, 192.0f  }, // Tế Đàn Vạn Yêu tại Đồi Cát Phong Sa
    .duelBannerPos   = { 147.0f,  0.0f, 212.0f  }, // Cờ Tỉ Thí ngay rìa Ốc Đảo An Toàn
    .defaultStartPos = { 160.0f,  0.0f, 222.0f  },
    .dunesCenter     = { 135.0f,  0.0f, 195.0f  },
    .canyonCenter    = { 94.10f,  0.0f, 150.0f  },
    .badlandsCenter  = { 65.0f,   0.0f, 95.0f   },
    .zoneCount = 4,
    .zones = {
        { { 170.68f, 0.0f, 230.41f }, 22.0f, "OC DAO HOANG SA [AN TOAN]" },
        { { 135.0f,  0.0f, 195.0f  }, 32.0f, "DOI CAT PHONG SA [TAN THU]" },
        { { 94.10f,  0.0f, 150.0f  }, 45.0f, "THACH LAM CO DAO [GIANG HO]" },
        { { 65.0f,   0.0f, 95.0f   }, 40.0f, "HUYET DAO HAC DIEN [HUNG HIEM]" }
    },
    .nodeCount = 16,
    .nodes = {
        // 6 Oasis Herbs around safe shore
        { OW_NODE_HERB, 184.0f, 236.0f, true },
        { OW_NODE_HERB, 176.0f, 244.0f, true },
        { OW_NODE_HERB, 160.0f, 241.0f, true },
        { OW_NODE_HERB, 156.0f, 225.0f, true },
        { OW_NODE_HERB, 165.0f, 217.0f, true },
        { OW_NODE_HERB, 179.0f, 219.0f, true },
        // 6 Earth Ore Veins
        { OW_NODE_ORE_VEIN, 144.0f, 206.0f, true },
        { OW_NODE_ORE_VEIN, 128.0f, 188.0f, true },
        { OW_NODE_ORE_VEIN, 108.0f, 166.0f, true },
        { OW_NODE_ORE_VEIN, 94.0f,  144.0f, true },
        { OW_NODE_ORE_VEIN, 82.0f,  158.0f, true },
        { OW_NODE_ORE_VEIN, 78.0f,  126.0f, true },
        // 4 Buried & Golden Relic Chests
        { OW_NODE_BURIED_CHEST,       122.0f, 178.0f, true },
        { OW_NODE_BURIED_CHEST,       98.0f,  132.0f, true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 72.0f,  104.0f, true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 58.0f,  88.0f,  false }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 149.0f, 0.0f, 210.0f },
        { 116.0f, 0.0f, 174.0f },
        { 105.0f, 0.0f, 138.0f },
        { 80.0f,  0.0f, 128.0f }
    },
    .campCount = 5,
    .camps = {
        { { 136.0f, 0.0f, 196.0f }, 6, 3, MONSTER_SWARM_MELEE },
        { { 126.0f, 0.0f, 184.0f }, 5, 3, MONSTER_SWARM_MELEE },
        { { 96.0f,  0.0f, 152.0f }, 4, 3, MONSTER_RANGED_CASTER },
        { { 84.0f,  0.0f, 138.0f }, 5, 3, MONSTER_LEAPER },
        { { 66.0f,  0.0f, 96.0f  }, 2, 3, MONSTER_ELITE_GUARD }
    },
    .hordeZoneCount = 7,
    .hordeZones = {
        // Starter Dunes: approachable swarmer packs
        { { 136.0f, 0.0f, 196.0f }, 18.0f, 3, 22, 46, 4.5f, 10, 0.15f, 0.10f, 0 },
        { { 120.0f, 0.0f, 178.0f }, 20.0f, 3, 26, 54, 4.2f, 12, 0.20f, 0.12f, 1 },
        // Relic Canyon: fast leapers + elemental spitters
        { { 96.0f,  0.0f, 152.0f }, 22.0f, 3, 30, 62, 3.8f, 14, 0.25f, 0.18f, 1 },
        { { 84.0f,  0.0f, 134.0f }, 20.0f, 3, 28, 58, 4.0f, 12, 0.22f, 0.16f, 1 },
        // Blood Badlands: dense Vampire-Survivors chaos + Alpha Brutes
        { { 65.0f,  0.0f, 95.0f  }, 24.0f, 3, 36, 75, 3.3f, 16, 0.28f, 0.18f, 2 },
        { { 112.0f, 0.0f, 118.0f }, 22.0f, 3, 28, 60, 4.0f, 12, 0.22f, 0.15f, 1 },
        { { 142.0f, 0.0f, 148.0f }, 20.0f, 3, 24, 52, 4.4f, 10, 0.18f, 0.12f, 1 }
    },
    .weatherProfileCount = 4,
    .weatherProfiles = {
        {
            .type                = OW_WEATHER_CLEAR,
            .name                = "NANG VANG GIO NHE",
            .startBanner         = "BAU TROI HOANG SA QUANG DANG — THICH HOP DAO CANH & KHAI KHOANG",
            .defaultDurationSec  = 65.0f,
            .atmoMode            = ATMO_MODE_DESERT_DUST,
            .windDirection       = { 0.85f, 0.52f },
            .windSpeedMps        = 3.4f,
            .windTurbulence      = 0.25f,
            .accentColor         = { 245, 218, 135, 255 },
            .moveSpeedBonus      = 0.0f,
            .xpMultiplier        = 1.0f,
            .revealHiddenNodes   = false,
            .spawnSpiritOrbs     = false,
            .spawnMeteorNodes    = false,
            .vortexSpawnInterval = 0.0f
        },
        {
            .type                = OW_WEATHER_STORM,
            .name                = "BAO CAT HOANG SA",
            .startBanner         = "CUONG PHONG BAO CAT! +25% TOC CHAY & LO DIEN RUONG CO MO!",
            .defaultDurationSec  = 50.0f,
            .atmoMode            = ATMO_MODE_DESERT_DUST,
            .windDirection       = { 0.92f, 0.38f },
            .windSpeedMps        = 11.5f,
            .windTurbulence      = 0.85f,
            .accentColor         = { 245, 175, 85, 255 },
            .moveSpeedBonus      = 0.25f,
            .xpMultiplier        = 1.25f,
            .revealHiddenNodes   = true,
            .spawnSpiritOrbs     = false,
            .spawnMeteorNodes    = false,
            .vortexSpawnInterval = 9.0f
        },
        {
            .type                = OW_WEATHER_SPIRIT_PHENOM,
            .name                = "HAI THI THAN LAU",
            .startBanner         = "AO ANH HAI THI THAN LAU! LINH CHAU XUAT HIEN & x1.5 EXP VO HOC!",
            .defaultDurationSec  = 50.0f,
            .atmoMode            = ATMO_MODE_SPIRIT_SPARKS,
            .windDirection       = { -0.60f, 0.80f },
            .windSpeedMps        = 4.5f,
            .windTurbulence      = 0.45f,
            .accentColor         = { 115, 240, 235, 255 },
            .moveSpeedBonus      = 0.10f,
            .xpMultiplier        = 1.50f,
            .revealHiddenNodes   = false,
            .spawnSpiritOrbs     = true,
            .spawnMeteorNodes    = false,
            .vortexSpawnInterval = 0.0f
        },
        {
            .type                = OW_WEATHER_CATACLYSM,
            .name                = "MUA THIEN THACH",
            .startBanner         = "THIEN THACH GIANG THE! KHOANG THACH HUYEN THOAI ROI KHAP SA MAC!",
            .defaultDurationSec  = 45.0f,
            .atmoMode            = ATMO_MODE_WAR_EMBERS,
            .windDirection       = { 0.45f, -0.89f },
            .windSpeedMps        = 7.8f,
            .windTurbulence      = 0.70f,
            .accentColor         = { 255, 125, 70, 255 },
            .moveSpeedBonus      = 0.10f,
            .xpMultiplier        = 1.35f,
            .revealHiddenNodes   = false,
            .spawnSpiritOrbs     = false,
            .spawnMeteorNodes    = true,
            .vortexSpawnInterval = 0.0f
        }
    },
    .spawnerPresetCount = 6,
    .spawnerPresets = {
        {
            .label       = "NHA BAY SA YEU (36 QUAI)",
            .bannerText  = "TE DAN VAN YEU NHA BAY SA YEU! SONG QUAI TRAN TOI!",
            .actionKind  = OW_SPAWN_MONSTER_PACK,
            .monsterKind = MONSTER_SWARM_MELEE,
            .count       = 36,
            .element     = 3,
            .bossVariant = 0
        },
        {
            .label       = "TRIEU HOI SONG YEU (80 QUAI)",
            .bannerText  = "DAI TRAN CUONG YEU! 80 QUAI TRAN RA KHAP SA MAC!",
            .actionKind  = OW_SPAWN_MONSTER_PACK,
            .monsterKind = MONSTER_SWARM_MELEE,
            .count       = 80,
            .element     = 3,
            .bossVariant = 0
        },
        {
            .label       = "GOI BOSS HAC DIEN TON GIA",
            .bannerText  = "HAC DIEN TON GIA GIANG LAM SA MAC!",
            .actionKind  = OW_SPAWN_WORLD_BOSS,
            .monsterKind = MONSTER_ELITE_GUARD,
            .count       = 1,
            .element     = 3,
            .bossVariant = 0
        },
        {
            .label       = "GOI BOSS MOC LINH CO THU",
            .bannerText  = "MOC LINH CO THU THUC TINH GIUA SA MAC!",
            .actionKind  = OW_SPAWN_WORLD_BOSS,
            .monsterKind = MONSTER_ELITE_GUARD,
            .count       = 1,
            .element     = 1,
            .bossVariant = 1
        },
        {
            .label       = "MOI CAO THU TY THI (1v1)",
            .bannerText  = "CAO THU GIANG HO NHAP LOI DAI TY THI! (DUNG O 1 HP)",
            .actionKind  = OW_SPAWN_AI_RIVAL_SPAR,
            .monsterKind = MONSTER_SWARM_MELEE,
            .count       = 1,
            .element     = 3,
            .bossVariant = 0
        },
        {
            .label       = "GOI AC NHAN DO SAT (PK)",
            .bannerText  = "AC NHAN HUYET SAT XUAT HIEN! CHIEN DAU SINH TU!",
            .actionKind  = OW_SPAWN_AI_RIVAL_PK,
            .monsterKind = MONSTER_SWARM_MELEE,
            .count       = 1,
            .element     = 2,
            .bossVariant = 0
        }
    },
    .pvpConfig = {
        .sparRingRadius        = 12.0f,
        .sparRewardGold        = 60,
        .sparRewardSkillXp     = 60,
        .slaughterDmgBonus     = 0.08f,
        .slaughterSpeedBonus   = 0.15f,
        .bountyInfamyThreshold = 2
    }
};

const OWRealmDef *OWRealm_GetDesertEarth(void) {
    return &kDesertEarthRealm;
}
