// open_world/ow_sandbox_modes.h
// Generic Open-World Drop-in/Drop-out Activity Engine:
//   - 3-State PK System (Hòa Bình / Tỉ Thí 1v1 / Đồ Sát Sát Khí & Truy Nã)
//   - Data-driven Monster / Boss / AI Rival Spawner (Tế Đàn Vạn Yêu & [B] Quick Panel)
//   - On-demand Weather & Sandbox Cleanup controls
#ifndef OW_SANDBOX_MODES_H
#define OW_SANDBOX_MODES_H

#include "open_world/ow_types.h"
#include "sandbox/sandbox_core.h"

void OWSandbox_Init(const OWRealmDef *realm);
void OWSandbox_Update(PlayerEntity *player, float dt);
void OWSandbox_Draw3D(const PlayerEntity *player, Camera3D cam);
void OWSandbox_DrawHUD(const PlayerEntity *player, Camera3D cam);
void OWSandbox_ClearCustomSpawns(void);

// Cycle PK mode: PEACE -> SPAR -> SLAUGHTER -> PEACE
void OWSandbox_CyclePkMode(PlayerEntity *player);
// Directly set a PK mode (enforces Safe Zone check)
bool OWSandbox_SetPkMode(PlayerEntity *player, AgentPkMode mode);
const char *OWSandbox_GetPkModeLabel(AgentPkMode mode);

// Execute a data-driven spawner preset from OWRealmDef->spawnerPresets[]
bool OWSandbox_TriggerPreset(PlayerEntity *player, int presetIndex);
int  OWSandbox_GetActiveCustomSpawnCount(void);

// Quick Panel ([B]) state
bool OWSandbox_IsPanelOpen(void);
void OWSandbox_TogglePanel(void);

// Nearby world-object prompt ([F] at Altar or Duel Banner)
const char *OWSandbox_GetNearbyPrompt(Vector3 playerPos);
bool        OWSandbox_TryInteract(PlayerEntity *player);

// Optional Slaughter PvE bonus multipliers
float OWSandbox_GetSlaughterDamageMult(const Agent *pa);
float OWSandbox_GetSlaughterSpeedMult(const Agent *pa);

#endif // OW_SANDBOX_MODES_H
