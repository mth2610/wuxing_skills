// open_world/ow_types.h
// Pure data-contract definitions for the Open-World Engine/Data Split.
// Realm files (open_world/realms/*.c) populate OWRealmDef tables;
// engine modules (ow_world, ow_weather, ow_sandbox_modes, ow_loot, ow_quest_npc)
// consume them with zero hardcoded realm-specific constants.
#ifndef OW_TYPES_H
#define OW_TYPES_H

#include "ai/ai.h"
#include "environment/atmosphere.h"
#include "raylib.h"
#include <stdbool.h>

#define OW_MAX_ZONES            4
#define OW_MAX_NODE_SPAWNS      24
#define OW_MAX_WIND_SURGES      4
#define OW_MAX_CAMPS            16
#define OW_MAX_WEATHER_PROFILES 4
#define OW_MAX_SPAWNER_PRESETS  6

typedef enum {
    OW_NODE_ORE_VEIN = 0,
    OW_NODE_HERB,
    OW_NODE_BURIED_CHEST,
    OW_NODE_GOLDEN_RELIC_CHEST
} OWNodeKind;

typedef enum {
    OW_LOOT_GOLD = 0,
    OW_LOOT_STONE,
    OW_LOOT_HERB,
    OW_LOOT_POTION_HEAL,
    OW_LOOT_DATAU_TOKEN,
    OW_LOOT_SKILL_XP
} OWLootKind;

typedef struct {
    Vector3     center;
    float       radius;
    const char *label;
} OWZoneDef;

typedef struct {
    OWNodeKind kind;
    float      x, z;
    bool       availableAtStart;
} OWNodeSpawn;

typedef struct {
    Vector3     center;
    int         count;
    int         element;
    MonsterKind kind;
} OWCampSpawn;

// --- Data-Driven Weather Profile (consumed by ow_weather.c) ---
typedef enum {
    OW_WEATHER_CLEAR = 0,     // Trời quang gió nhẹ
    OW_WEATHER_STORM,         // Bão nguyên tố (Bão Cát / Mưa Bão...)
    OW_WEATHER_SPIRIT_PHENOM, // Hiện tượng linh khí (Hải Thị Thận Lâu / Mưa Hoa...)
    OW_WEATHER_CATACLYSM      // Thiên tai khoáng thạch (Mưa Sao Băng / Bão Kiếm...)
} OWWeatherType;

typedef struct {
    OWWeatherType  type;
    const char    *name;                // HUD display label
    const char    *startBanner;         // Banner when weather activates
    float          defaultDurationSec;  // Cycle duration (seconds); <= 0 uses engine default
    AtmosphereMode atmoMode;            // Particle atmosphere mode
    Vector2        windDirection;       // Normalized XZ wind direction
    float          windSpeedMps;        // Macro wind speed (m/s)
    float          windTurbulence;      // Macro turbulence factor
    Color          accentColor;         // Weather HUD/FX accent tint
    float          moveSpeedBonus;      // Optional tailwind speed bonus (e.g. +0.25f)
    float          xpMultiplier;        // Skill XP multiplier during weather (e.g. 1.5f)
    bool           revealHiddenNodes;   // Uncover buried golden relic chests on start
    bool           spawnSpiritOrbs;     // Spawn collectible Mirage/Spirit orbs in field
    bool           spawnMeteorNodes;    // Spawn crackable Meteorite/Mineral nodes
    float          vortexSpawnInterval; // Seconds between wind vortices (0 = disabled)
} OWWeatherProfileDef;

// --- Data-Driven Spawner Preset (consumed by ow_sandbox_modes.c) ---
typedef enum {
    OW_SPAWN_MONSTER_PACK = 0, // Nhả bầy quái (Tiểu Yêu / Pháp Yêu / Tinh Anh)
    OW_SPAWN_WORLD_BOSS,       // Triệu hồi Đại Tinh Linh Boss
    OW_SPAWN_AI_RIVAL_SPAR,    // Triệu hồi Cao Thủ AI để Tỉ Thí 1v1 (không chết)
    OW_SPAWN_AI_RIVAL_PK       // Triệu hồi Ác Nhân AI để Đồ Sát sinh tử
} OWSpawnActionKind;

typedef struct {
    const char       *label;       // Button label in Quick Panel
    const char       *bannerText;  // Banner shown on activation
    OWSpawnActionKind actionKind;
    MonsterKind       monsterKind; // Used for OW_SPAWN_MONSTER_PACK
    int               count;       // Spawn count
    int               element;     // 0..4 element
    int               bossVariant; // 0=Hắc Diện Tôn Giả, 1=Mộc Linh
} OWSpawnerPresetDef;

// --- Data-Driven PvP / PK Tuning (consumed by ow_sandbox_modes.c) ---
typedef struct {
    float sparRingRadius;        // Default 12.0m
    int   sparRewardGold;        // Default 60
    int   sparRewardSkillXp;     // Default 60
    float slaughterDmgBonus;     // Bonus damage per infamy stack (e.g. 0.08f)
    float slaughterSpeedBonus;   // Bonus speed while in Slaughter mode (e.g. 0.15f)
    int   bountyInfamyThreshold; // Infamy level that spawns a Bounty Enforcer (e.g. 2)
} OWPvPConfig;

// --- Complete Realm Descriptor ---
typedef struct {
    int                 realmId;       // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    Vector3             safeCenter;
    float               safeRadius;
    Vector3             daTauNpcPos;
    Vector3             alchemistPos;
    Vector3             stelePos;
    Vector3             altarPos;      // Tế Đàn Vạn Yêu (3D world spawner altar)
    Vector3             duelBannerPos; // Cờ Tỉ Thí 3D
    Vector3             defaultStartPos;
    Vector3             dunesCenter;
    Vector3             canyonCenter;
    Vector3             badlandsCenter;
    OWZoneDef           zones[OW_MAX_ZONES];
    int                 zoneCount;
    OWNodeSpawn         nodes[OW_MAX_NODE_SPAWNS];
    int                 nodeCount;
    Vector3             windSurges[OW_MAX_WIND_SURGES];
    int                 windSurgeCount;
    OWCampSpawn         camps[OW_MAX_CAMPS];
    int                 campCount;
    AIHordeZoneDef      hordeZones[OW_MAX_CAMPS];
    int                 hordeZoneCount;
    bool                scaleWithMap;  // true = authored at 1x base coords, scaled by MapManager_GetWorldScaleXZ()
    OWWeatherProfileDef weatherProfiles[OW_MAX_WEATHER_PROFILES];
    int                 weatherProfileCount;
    OWSpawnerPresetDef  spawnerPresets[OW_MAX_SPAWNER_PRESETS];
    int                 spawnerPresetCount;
    OWPvPConfig         pvpConfig;
} OWRealmDef;

#endif // OW_TYPES_H
