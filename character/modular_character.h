// character/modular_character.h
// Modular Character Customization & Swarm Minion System for Wuxing Skills.
//
// Features:
// 1. Decoupled Modular Slots: Base body, clothing (torso, legs, boots, bracers),
//    rigid sockets (headgear, mask, hair, waist, back, weapons), and Full Outfits.
// 2. Full Outfit & Body Masking: Full robes occupy multiple slots (occupiedSlotsMask)
//    and suppress underlying modular clothes and base body mesh to eliminate clipping.
// 3. Shared Skeleton & Animation Retargeting: Reuses standard humanoid rig across
//    all characters (hero, boss, minion) without duplicate skeletons or memory bloat.
// 4. Clustered Minion Rendering: Evaluates CPU pose once for a swarm type, then
//    renders 50+ minion instances with custom scale/position/tint at 60+ FPS.

#ifndef MODULAR_CHARACTER_H
#define MODULAR_CHARACTER_H

#include "raylib.h"
#include "character/character_model.h"
#include <stdbool.h>
#include <stdint.h>

#define MODULAR_MAX_SKINNED_PARTS  8
#define MODULAR_MAX_SOCKETS        8
#define MODULAR_MAX_ITEMS        128
#define MODULAR_MAX_MINION_BATCH  64

// Equipment slots (13 granular slots supporting Modular & Full Outfits)
typedef enum {
    MODULAR_SLOT_BASE_BODY = 0, // Thân thể (Master skeleton & naked mesh)
    MODULAR_SLOT_HAIR,          // Mái tóc / Trâm cài (Hair & Hairpins)
    MODULAR_SLOT_HEADGEAR,      // Nón lá, mũ quan, khăn đóng
    MODULAR_SLOT_MASK,          // Mặt nạ, khăn che mặt dạ hành
    MODULAR_SLOT_TORSO,         // Y phục (Áo trong, áo giao lĩnh, giáp ngực)
    MODULAR_SLOT_LEGS,          // Hạ phục (Quần võ hiệp, thường phục)
    MODULAR_SLOT_BOOTS,         // Chiến hài, giày da, hia
    MODULAR_SLOT_BRACERS,       // Hộ uyển, bao tay thép
    MODULAR_SLOT_WAIST,         // Đai lưng, ngọc bội, túi gấm
    MODULAR_SLOT_BACK,          // Phi phong, áo choàng, vỏ kiếm sau lưng
    MODULAR_SLOT_WEAPON_R,      // Binh khí tay phải (Kiếm / Đao / Quyền)
    MODULAR_SLOT_WEAPON_L,      // Binh khí tay trái (Khiên / Đoản kiếm)
    MODULAR_SLOT_FULL_OUTFIT,   // Trang phục nguyên bộ (Trường bào, Đạo bào toàn thân)
    MODULAR_SLOT_COUNT
} ModularSlot;

// Occupied slot bitmasks for Full Outfits
#define MODULAR_OCCUPIED_TORSO    (1 << MODULAR_SLOT_TORSO)
#define MODULAR_OCCUPIED_LEGS     (1 << MODULAR_SLOT_LEGS)
#define MODULAR_OCCUPIED_BOOTS    (1 << MODULAR_SLOT_BOOTS)
#define MODULAR_OCCUPIED_BRACERS  (1 << MODULAR_SLOT_BRACERS)
#define MODULAR_OCCUPIED_OUTFIT   (MODULAR_OCCUPIED_TORSO | MODULAR_OCCUPIED_LEGS)

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
    uint32_t           occupiedSlotsMask; // Bitmask of slots suppressed by Full Outfit
    bool               hideBaseBody;      // Suppress base naked body to eliminate clipping
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

// Calculates world transform for rigid socket attachment (fallback to calibrated socket anchors)
bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix);

// Calculates world transform for socket attachment with skeletal bone tracking
bool ModularSystem_GetSocketMatrixEx(Vector3 charPos, float charYaw, float charScale,
                                     ModularSlot slot, const char *boneSocket,
                                     const CharacterAnimState *animState,
                                     Matrix localOffset, Matrix *outMatrix);

// -----------------------------------------------------------------------------
// High-Scale Swarm Minion Batch Rendering (50+ Minions at 60+ FPS)
// -----------------------------------------------------------------------------

void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex,
                                  const CharacterAnimState *sharedAnimState,
                                  const ModularMinionBatchItem *items, int itemCount);

#endif // MODULAR_CHARACTER_H
