// character/modular_character.h
// High-performance Modular 3D Asset & Animation Management System for Wuxing Skills.
//
// Supports:
// 1. Online Heroes (up to 4 players): Full modularity (Body + Torso/Robe + Legs/Pants + Sockets).
// 2. Bosses (up to 2 bosses): Heavy boss rigs + boss skill animations + weapon sockets.
// 3. Swarm Minions (50+ minions): Clustered animation evaluation (1 skinning pass for entire squad).
// 4. Shared Animation Library: Universal Mixamo Humanoid rig contract.
// 5. Zero-Skinning Rigid Sockets: Weapons, Hats (Nón Lá), Scabbards attach via bone matrices.

#ifndef MODULAR_CHARACTER_H
#define MODULAR_CHARACTER_H

#include "raylib.h"
#include "character/character_model.h"
#include <stdbool.h>

#define MODULAR_MAX_SKINNED_PARTS  4
#define MODULAR_MAX_SOCKETS       6
#define MODULAR_MAX_ITEMS       128
#define MODULAR_MAX_MINION_BATCH 64

// Equipment slots
typedef enum {
    MODULAR_SLOT_BASE_BODY = 0, // Master skeleton & naked mesh (hero, boss, minion)
    MODULAR_SLOT_TORSO,         // Skinned clothing (áo giao lĩnh, giáp ngực)
    MODULAR_SLOT_LEGS,          // Skinned clothing (quần wuxia, giáp chân)
    MODULAR_SLOT_WEAPON_R,      // Rigid socket: Right hand (kiếm, đao, quyền)
    MODULAR_SLOT_WEAPON_L,      // Rigid socket: Left hand (khiên, đoản kiếm)
    MODULAR_SLOT_HEADGEAR,      // Rigid socket: Head (nón lá, khăn đóng, mặt nạ)
    MODULAR_SLOT_BACK,          // Rigid socket: Spine2 (vỏ kiếm, phi kiếm, cánh)
    MODULAR_SLOT_COUNT
} ModularSlot;

// Entity performance tier
typedef enum {
    MODULAR_TIER_HERO = 0, // Max 4 players: dynamic per-instance deformation + full sockets
    MODULAR_TIER_BOSS,     // Max 2 bosses: high-detail mesh + boss-specific sockets
    MODULAR_TIER_MINION    // Up to 50 swarm units: clustered pose sharing (single skinning pass)
} ModularTier;

// Registered item in the asset library
typedef struct {
    char        id[48];
    ModularSlot slot;
    bool        isSkinned;      // true = deforming clothing, false = rigid socket attachment
    char        boneSocket[32]; // For sockets: name of parent bone (e.g. "mixamorig:RightHand")
    Matrix      localOffset;    // Local translation/rotation at attachment point
    Model       model;
    bool        loaded;
} ModularItemDef;

// Active loadout for an entity (what it wears and wields)
typedef struct {
    int itemIndices[MODULAR_SLOT_COUNT]; // index into s_itemRegistry (-1 if empty)
} ModularLoadout;

// Entity instance state
typedef struct {
    ModularTier        tier;
    ModularLoadout     loadout;
    CharacterAnimState animState;
    Vector3            position;
    float              yaw;
    float              scale;
    Color              tint;
    bool               active;
} ModularInstance;

// Clustered minion render item
typedef struct {
    Vector3 position;
    float   yaw;
    float   scale;
    Color   tint;
} ModularMinionBatchItem;

// -----------------------------------------------------------------------------
// System Lifecycle & Asset Registry
// -----------------------------------------------------------------------------

void ModularSystem_Init(void);
void ModularSystem_Shutdown(void);

// Registers an item (body, clothing, weapon, hat) into the library
int  ModularSystem_RegisterItem(const char *id, ModularSlot slot, bool isSkinned,
                                const char *boneSocket, Vector3 offsetPos, Vector3 offsetRotEuler,
                                const char *modelGlbPath);

// Lookup item by ID
int  ModularSystem_FindItem(const char *id);

// -----------------------------------------------------------------------------
// Instance Management
// -----------------------------------------------------------------------------

// Initializes an instance with a default loadout
void ModularInstance_Init(ModularInstance *inst, ModularTier tier);

// Equips an item into a slot
bool ModularInstance_Equip(ModularInstance *inst, ModularSlot slot, const char *itemId);
bool ModularInstance_EquipIndex(ModularInstance *inst, ModularSlot slot, int itemIndex);

// Advances animation playback for an instance
void ModularInstance_Update(ModularInstance *inst, float dt, bool isMoving);

// Draws a Hero or Boss instance with all equipped clothing and socketed attachments
void ModularInstance_Draw(ModularInstance *inst);

// -----------------------------------------------------------------------------
// High-Scale Swarm Minion Batch Rendering (50+ Minions at 60+ FPS)
// -----------------------------------------------------------------------------

// Draws an entire swarm batch of minions sharing the same body/armor/weapon.
// Evaluates the animation pose ONCE on CPU, then renders all N positions.
void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex,
                                  const CharacterAnimState *sharedAnimState,
                                  const ModularMinionBatchItem *items, int itemCount);

// -----------------------------------------------------------------------------
// Utility Helpers
// -----------------------------------------------------------------------------

// Computes the world matrix of a specific socket in an animated character
bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix);

#endif // MODULAR_CHARACTER_H
