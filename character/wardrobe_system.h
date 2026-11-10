// character/wardrobe_system.h
// Comprehensive Wardrobe, Inventory Collection & Customization System for Wuxing Skills.
//
// Manages:
// 1. Item Catalog: Declarative definitions of bodies, clothing, armor, accessories, full outfits, and swords.
// 2. Personal Collection & Online Progression: Persistent inventory of unlocked items (exploration, boss drops, crafting).
// 3. Combat Attributes & Perks: Stats (HP, MP, Speed, Elemental bonus/resist) and special martial arts perks.
// 4. Hybrid Outfits: Seamless coexistence of Modular pieces and Full Outfits with occupied slot suppression.
// 5. Outfit Plans: Saving and instant-switching 5 appearance presets (Where Winds Meet style).
// 6. Combat Stance: Binary weapon philosophy (Swords vs Unarmed/Tay Không).
// 7. Save/Load Persistence: Storing wardrobe progress to JSON.
// 8. Seamless 3D Binding: Syncing equipped loadout directly to ModularInstance.

#ifndef WARDROBE_SYSTEM_H
#define WARDROBE_SYSTEM_H

#include "raylib.h"
#include "character/modular_character.h"
#include <stdbool.h>
#include <stdint.h>

#define WARDROBE_MAX_CATALOG_ITEMS 256
#define WARDROBE_MAX_COLLECTION    128
#define WARDROBE_ID_MAX_LEN         48
#define WARDROBE_NAME_MAX_LEN       96
#define WARDROBE_DESC_MAX_LEN      256
#define WARDROBE_MAX_PRESETS         5

// Wuxing Elemental Affinity
typedef enum {
    WARDROBE_ELEM_NONE = 0, // Vô Hệ / Phổ thông
    WARDROBE_ELEM_METAL,    // Kim
    WARDROBE_ELEM_WOOD,     // Mộc
    WARDROBE_ELEM_WATER,    // Thủy
    WARDROBE_ELEM_FIRE,     // Hỏa
    WARDROBE_ELEM_EARTH,    // Thổ
    WARDROBE_ELEM_TAIJI     // Thái Cực
} WardrobeElement;

// Item Rarity Tier
typedef enum {
    WARDROBE_RARITY_COMMON = 1,    // Phổ Thông (Bạch Ngân)
    WARDROBE_RARITY_UNCOMMON,      // Tinh Xảo (Bích Ngọc)
    WARDROBE_RARITY_RARE,          // Hiếm (Lam Tinh)
    WARDROBE_RARITY_EPIC,          // Sử Thi (Tử Hà)
    WARDROBE_RARITY_LEGENDARY      // Truyền Thuyết (Hoàng Kim)
} WardrobeRarity;

// Item Type: Modular piece vs Full Outfit
typedef enum {
    ITEM_TYPE_MODULAR = 0, // Món lẻ ghép tự do (Áo, Quần, Giày, Nón, Kiếm...)
    ITEM_TYPE_FULL_OUTFIT  // Trang phục nguyên bộ (Trường bào, Đạo bào toàn thân)
} WardrobeItemType;

// Unlock / Acquisition Method (Online progression)
typedef enum {
    UNLOCK_TYPE_DEFAULT = 0,     // Khởi đầu có sẵn (Áo vải, Quần thô)
    UNLOCK_TYPE_EXPLORATION,     // Thu thập / Khám phá giang hồ (Nhặt ven sông, mở rương)
    UNLOCK_TYPE_BOSS_DROP,       // Phần thưởng hạ Boss (Mộc Linh, Hắc Diện)
    UNLOCK_TYPE_CRAFTING,        // Rèn đúc từ khoáng thạch & kỳ trân
    UNLOCK_TYPE_ACHIEVEMENT      // Thành tựu võ học / Đột phá cảnh giới
} WardrobeUnlockType;

// Combat Stance derived from weapon slot
typedef enum {
    COMBAT_STANCE_UNARMED = 0, // Tay Không: Quyền, Chưởng, Cước, Vận Khí, Thái Cực
    COMBAT_STANCE_SWORD        // Kiếm Pháp: Đơn Kiếm, Chém, Đâm, Đỡ, Gạt Đòn
} CombatStance;

// Combat Attribute Modifiers (Sẵn sàng cho việc cộng lực, thủ, tốc độ, chân khí, ngũ hành)
typedef struct {
    float bonusHealth;       // Sinh lực tối đa (+HP)
    float bonusMana;         // Chân khí tối đa (+MP)
    float speedMultiplier;   // Thân pháp / Tốc độ di chuyển (1.0 = 100%, 1.05 = +5%)
    float elementalBonus;    // Sát thương Ngũ Hành tương hợp (+% damage)
    float elementalResist;   // Kháng sát thương Ngũ Hành (+% resistance)
    float manaRegenBonus;    // Tốc độ hồi phục chân khí Thiền Định (+% mana regen)
    char  specialPerk[128];  // Mô tả hiệu ứng đặc biệt (e.g. "Khai mở chiêu thức Kiếm Pháp")
} WardrobeItemStats;

// Item Catalog Entry
typedef struct {
    char                id[WARDROBE_ID_MAX_LEN];
    char                name[WARDROBE_NAME_MAX_LEN];
    char                description[WARDROBE_DESC_MAX_LEN];
    char                source[WARDROBE_DESC_MAX_LEN]; // e.g. "Bờ cát sông Quao", "Hạ Boss Mộc Linh"
    ModularSlot         slot;
    WardrobeItemType    itemType;
    uint32_t            occupiedSlots;  // Bitmask of slots occupied if FULL_OUTFIT
    bool                hideBaseBody;   // Suppress base naked body to eliminate clipping
    WardrobeElement     element;
    WardrobeRarity      rarity;
    WardrobeUnlockType  unlockType;     // Cách thức thu thập trong game online
    char                unlockHint[128];// Gợi ý mở khóa (e.g. "Hạ gục Boss Mộc Linh")
    WardrobeItemStats   stats;          // Chỉ số sức mạnh & hiệu ứng chiến đấu
    bool                isSkinned;      // true = deforming cloth, false = rigid socket
    char                boneSocket[32]; // e.g. "mixamorig:RightHand"
    Vector3             socketOffsetPos;
    Vector3             socketOffsetRot;
    char                modelPath[128];
    Color               uiColor;
} WardrobeCatalogItem;

// Saved Outfit Preset / Appearance Plan (5 Bộ Diện Mạo Lưu Sẵn)
typedef struct {
    char name[32];
    char equippedIds[MODULAR_SLOT_COUNT][WARDROBE_ID_MAX_LEN];
} WardrobeOutfitPreset;

// Player's Personal Wardrobe & Collection State
typedef struct {
    char                 playerId[32];
    char                 equippedIds[MODULAR_SLOT_COUNT][WARDROBE_ID_MAX_LEN];
    char                 unlockedIds[WARDROBE_MAX_COLLECTION][WARDROBE_ID_MAX_LEN];
    int                  unlockedCount;
    CombatStance         activeStance;
    WardrobeOutfitPreset presets[WARDROBE_MAX_PRESETS];
    int                  activePresetIndex;
} PlayerWardrobe;

// -----------------------------------------------------------------------------
// Catalog Management
// -----------------------------------------------------------------------------

void Wardrobe_InitCatalog(void);
void Wardrobe_ShutdownCatalog(void);
bool Wardrobe_RegisterItem(const WardrobeCatalogItem *item);
const WardrobeCatalogItem* Wardrobe_GetCatalogItem(const char *id);
const WardrobeCatalogItem* Wardrobe_GetCatalogItemByIndex(int index);
int  Wardrobe_GetCatalogCount(void);
bool Wardrobe_LoadCatalogFromFile(const char *jsonPath);

// -----------------------------------------------------------------------------
// Player Wardrobe & Customization Operations
// -----------------------------------------------------------------------------

void PlayerWardrobe_Init(PlayerWardrobe *pw, const char *playerId);
bool PlayerWardrobe_HasItem(const PlayerWardrobe *pw, const char *itemId);
bool PlayerWardrobe_UnlockItem(PlayerWardrobe *pw, const char *itemId);
bool PlayerWardrobe_Equip(PlayerWardrobe *pw, const char *itemId);
bool PlayerWardrobe_Unequip(PlayerWardrobe *pw, ModularSlot slot);
const char* PlayerWardrobe_GetEquippedId(const PlayerWardrobe *pw, ModularSlot slot);
CombatStance PlayerWardrobe_GetCombatStance(const PlayerWardrobe *pw);
bool PlayerWardrobe_ApplyToInstance(const PlayerWardrobe *pw, ModularInstance *inst);

// Aggregated Combat Attributes across all equipped gear
WardrobeItemStats PlayerWardrobe_GetTotalStats(const PlayerWardrobe *pw);

// Outfit Presets (1..5)
bool PlayerWardrobe_SavePreset(PlayerWardrobe *pw, int presetIndex, const char *presetName);
bool PlayerWardrobe_LoadPreset(PlayerWardrobe *pw, int presetIndex);

// Persistence (Save / Load)
bool PlayerWardrobe_SaveToFile(const PlayerWardrobe *pw, const char *filePath);
bool PlayerWardrobe_LoadFromFile(PlayerWardrobe *pw, const char *filePath);

// -----------------------------------------------------------------------------
// Global Wardrobe State API (Engine-wide Unified Customization & Persistence)
// -----------------------------------------------------------------------------
#define WARDROBE_DEFAULT_SAVE_PATH "assets/saves/player_wardrobe.json"

void             Wardrobe_InitGlobal(const char *savePath);
PlayerWardrobe*  Wardrobe_GetGlobalPlayer(void);
ModularInstance* Wardrobe_GetGlobalModularInstance(void);
bool             Wardrobe_SaveGlobal(void);
bool             Wardrobe_ApplyGlobal(void);
bool             Wardrobe_EquipGlobal(const char *itemId);
bool             Wardrobe_UnequipGlobal(ModularSlot slot);

#endif // WARDROBE_SYSTEM_H
