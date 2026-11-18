// open_world/ow_world.h
// Modular Open-World Core (Shared across all 5 Wuxing Realms):
//   - Realm data-driven configuration (OWRealmDef)
//   - Safe Zone Sanctum, Wind Surges, Guiding Wind VFX (VFX_ComposeGuidingWind)
//   - Use-based Skill XP, Kill Streak Frenzy, Quick Potions, Social Emotes & P2P Trade
#ifndef OW_WORLD_H
#define OW_WORLD_H

#include "ai/ai.h"
#include "raylib.h"
#include "sandbox/sandbox_core.h"
#include <stdbool.h>

#define OW_MAX_ZONES       4
#define OW_MAX_NODE_SPAWNS 24
#define OW_MAX_WIND_SURGES 4
#define OW_MAX_CAMPS       8

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
    OW_LOOT_DATAU_TOKEN
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

typedef struct {
    int          realmId;       // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    Vector3      safeCenter;
    float        safeRadius;
    Vector3      daTauNpcPos;
    Vector3      alchemistPos;
    Vector3      stelePos;
    Vector3      defaultStartPos;
    Vector3      dunesCenter;
    Vector3      canyonCenter;
    Vector3      badlandsCenter;
    OWZoneDef    zones[OW_MAX_ZONES];
    int          zoneCount;
    OWNodeSpawn  nodes[OW_MAX_NODE_SPAWNS];
    int          nodeCount;
    Vector3      windSurges[OW_MAX_WIND_SURGES];
    int          windSurgeCount;
    OWCampSpawn  camps[OW_MAX_CAMPS];
    int          campCount;
} OWRealmDef;

void OW_Init(const OWRealmDef *realm, bool spawnCamps);
void OW_Update(PlayerEntity *player, float dt);
void OW_Draw3D(const PlayerEntity *player, Camera3D cam);
void OW_DrawHUD(const PlayerEntity *player, Camera3D cam);

int         OW_GetZoneAt(Vector3 pos);
const char *OW_GetZoneName(int zoneIndex);
bool        OW_IsInSafeZone(Vector3 pos);
float       OW_GetMightTimer(void);
float       OW_GetXpBoostTimer(void);
int         OW_GetKillStreak(void);
Vector3     OW_GetGuideTarget(Vector3 playerPos);
void        OW_TriggerGuidingWind(void);
void        OW_SyncQuestEncounter(void);
bool        OW_TryInteractNearest(Vector3 playerPos);
int         OW_GetActiveNodeCount(void);
void        OW_TriggerSandstormEvent(void);
Vector3     OW_GetDaTauNpcPos(void);
void        OW_SetBanner(const char *msg, float seconds);

#endif // OW_WORLD_H
