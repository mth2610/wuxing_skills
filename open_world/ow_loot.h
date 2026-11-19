// open_world/ow_loot.h
// World Forage Nodes (Ore Veins, Herbs, Buried & Golden Relic Chests) + Magnetic 3D Loot Orbs.
#ifndef OW_LOOT_H
#define OW_LOOT_H

#include "open_world/ow_world.h"

void OWLoot_Init(const OWRealmDef *realm);
void OWLoot_Update(const Agent *playerAgent, float dt);
void OWLoot_Draw3D(Camera3D cam);

void OWLoot_SpawnOrb(Vector3 pos, OWLootKind kind, int amount);
bool OWLoot_TryHarvestNearest(Vector3 playerPos);
int  OWLoot_GetActiveNodeCount(void);
void OWLoot_TriggerSandstormReveal(void);
bool OWLoot_GetNearestAvailableNodePos(Vector3 fromPos, Vector3 *outPos);
const char *OWLoot_GetNearbyPrompt(Vector3 playerPos);

#endif // OW_LOOT_H
