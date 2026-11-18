// open_world/ow_quest_npc.h
// Open-World 3D NPCs (Dã Tẩu, Luyện Đan Sư), Training Stele, and Dynamic Quest Encounters.
#ifndef OW_QUEST_NPC_H
#define OW_QUEST_NPC_H

#include "open_world/ow_world.h"

void OWQuestNpc_Init(const OWRealmDef *realm, bool spawnStele);
void OWQuestNpc_Update(const Agent *playerAgent, float dt);
void OWQuestNpc_Draw3D(void);
void OWQuestNpc_DrawHUD(const Agent *playerAgent, Camera3D cam);

void OWQuestNpc_SyncEncounter(void);
bool OWQuestNpc_TryInteract(Vector3 playerPos);
bool OWQuestNpc_HandleTap(Vector3 playerPos, Vector2 tapScreenPos, Camera3D cam);
bool OWQuestNpc_GetEncounterGuideTarget(Vector3 playerPos, Vector3 *outPos);
const char *OWQuestNpc_GetNearbyPrompt(Vector3 playerPos);
int  OWQuestNpc_GetSteleAgentId(void);

#endif // OW_QUEST_NPC_H
