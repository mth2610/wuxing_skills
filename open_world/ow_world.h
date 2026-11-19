// open_world/ow_world.h
// Modular Open-World Core (Shared across all 5 Wuxing Realms):
//   - Realm data-driven configuration (OWRealmDef in ow_types.h)
//   - Safe Zone Sanctum, Wind Surges, Guiding Wind VFX (VFX_ComposeGuidingWind)
//   - Use-based Skill XP, Kill Streak Frenzy, Quick Potions, Social Emotes & P2P Trade
//   - Delegates Weather (ow_weather.h) & Sandbox PvP/Spawner (ow_sandbox_modes.h)
#ifndef OW_WORLD_H
#define OW_WORLD_H

#include "open_world/ow_types.h"
#include "sandbox/sandbox_core.h"
#include <stdbool.h>

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
void        OW_RecordKills(int count);
Vector3     OW_GetGuideTarget(Vector3 playerPos);
void        OW_TriggerGuidingWind(void);
void        OW_SyncQuestEncounter(void);
bool        OW_TryInteractNearest(Vector3 playerPos);
int         OW_GetActiveNodeCount(void);
void        OW_TriggerSandstormEvent(void);
Vector3     OW_GetDaTauNpcPos(void);
void        OW_SetBanner(const char *msg, float seconds);

#endif // OW_WORLD_H
