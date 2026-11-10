# Character Module — API Reference

> Pure interface reference for the `character/` module.
> Governed by `DOC_ARCHITECTURE.md` (API archetype: pure signatures, contracts, invariants, no status or debugging logs).
> For runtime traps and lessons learned, see `character/docs/LANDMINES.md`.

---

## 1. Quick Reference

### Character Model & Skeletal Animations (`character/character_model.h`)
```c
bool CharacterModel_Load(const char *filePath);
bool CharacterModel_LoadEx(const char *meshPath, const char *animBankPath);
bool CharacterModel_IsLoaded(void);
void CharacterModel_ResetState(CharacterAnimState *state);
void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving);
void CharacterModel_Trigger(CharacterAnimState *state, CharacterAnimSlot slot);
void CharacterModel_TriggerTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds);
void CharacterModel_TriggerVariant(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex);
void CharacterModel_TriggerVariantTimed(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex, float seconds);
bool CharacterModel_TriggerByName(CharacterAnimState *state, const char *clipName, float seconds);
bool CharacterModel_TriggerByIndex(CharacterAnimState *state, int animIndex, float seconds);

void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint);
void CharacterModel_DrawCustom(Model customModel, const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint);
void CharacterModel_DrawCustomEx(Model customModel, const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint, bool deflateTorso, bool deflateLegs);

bool CharacterModel_GetBoneTransform(const char *boneName, const CharacterAnimState *state, Matrix *outTransform);
bool CharacterModel_SampleSurfacePoint(const CharacterAnimState *state, Vector3 *outPos, Vector3 *outNormal);
```

### Modular Character System (`character/modular_character.h`)
```c
void ModularSystem_Init(void);
void ModularSystem_Shutdown(void);
int  ModularSystem_RegisterItem(const char *id, ModularSlot slot, bool isSkinned, const char *boneSocket, Vector3 offsetPos, Vector3 offsetRotEuler, const char *modelGlbPath);
int  ModularSystem_FindItem(const char *id);

void ModularInstance_Init(ModularInstance *inst, ModularTier tier);
bool ModularInstance_Equip(ModularInstance *inst, ModularSlot slot, const char *itemId);
bool ModularInstance_EquipIndex(ModularInstance *inst, ModularSlot slot, int itemIndex);
void ModularInstance_Update(ModularInstance *inst, float dt, bool isMoving);
void ModularInstance_Draw(ModularInstance *inst);

bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale, ModularSlot slot, Matrix localOffset, Matrix *outMatrix);
bool ModularSystem_GetSocketMatrixEx(Vector3 charPos, float charYaw, float charScale, ModularSlot slot, const char *boneSocket, const CharacterAnimState *animState, Matrix localOffset, Matrix *outMatrix);

void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex, const CharacterAnimState *sharedAnimState, const ModularMinionBatchItem *items, int itemCount);
```

### Spring Bone Physics (`character/spring_bone.h`)
```c
void SpringBone_Init(SpringBoneSystem *sys);
void SpringBone_SetupWuxiaDefaults(SpringBoneSystem *sys);
int  SpringBone_Add(SpringBoneSystem *sys, const char *name, const char *parentBone, Vector3 localRestDir, float length, float stiffness, float damping, float gravityFactor);
int  SpringBone_AddCollider(SpringBoneSystem *sys, const char *boneName, Vector3 offset, float radius);
void SpringBone_Update(SpringBoneSystem *sys, const struct CharacterAnimState *animState, Vector3 charPos, float charYaw, float charScale, float dt, Vector3 windVelocity);
bool SpringBone_GetBoneWorldPos(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outTip);
bool SpringBone_GetBoneMatrix(const SpringBoneSystem *sys, const char *boneName, Matrix *outMatrix);
void SpringBone_Reset(SpringBoneSystem *sys);
void SpringBone_DrawDebug(const SpringBoneSystem *sys);
```

### Weapon Trail System (`character/weapon_trail.h`)
```c
void WeaponTrail_Init(WeaponTrail *trail);
void WeaponTrail_Reset(WeaponTrail *trail);
void WeaponTrail_SetColors(WeaponTrail *trail, Color glowColor, Color coreColor);
void WeaponTrail_Sample(WeaponTrail *trail, Matrix weaponWorldMat, Vector3 localBase, Vector3 localTip, float dt, bool forceEmit);
void WeaponTrail_Fade(WeaponTrail *trail, float dt);
void WeaponTrail_Draw(const WeaponTrail *trail);
```

### Player Wardrobe & Catalog (`character/wardrobe_system.h`)
```c
void             Wardrobe_InitCatalog(void);
int              Wardrobe_GetCatalogCount(void);
const WardrobeItemDef* Wardrobe_GetCatalogItem(int index);
const WardrobeItemDef* Wardrobe_FindCatalogItem(const char *id);

void             PlayerWardrobe_Init(PlayerWardrobe *pw, const char *playerId);
bool             PlayerWardrobe_UnlockItem(PlayerWardrobe *pw, const char *itemId);
bool             PlayerWardrobe_HasItem(const PlayerWardrobe *pw, const char *itemId);
bool             PlayerWardrobe_Equip(PlayerWardrobe *pw, const char *itemId);
bool             PlayerWardrobe_Unequip(PlayerWardrobe *pw, ModularSlot slot);
void             PlayerWardrobe_CalculateStats(const PlayerWardrobe *pw, WardrobeItemStats *outStats);
CombatStance     PlayerWardrobe_GetCombatStance(const PlayerWardrobe *pw);

bool             PlayerWardrobe_SaveToFile(const PlayerWardrobe *pw, const char *filePath);
bool             PlayerWardrobe_LoadFromFile(PlayerWardrobe *pw, const char *filePath);

void             Wardrobe_InitGlobal(void);
PlayerWardrobe*  Wardrobe_GetGlobalPlayer(void);
ModularInstance* Wardrobe_GetGlobalModularInstance(void);
void             Wardrobe_UpdateGlobal(float dt, bool isMoving);
void             Wardrobe_DrawGlobal(void);
```

---

## 2. Core Concepts & Architecture

The `character/` module is the visual and rendering counterpart to `entities/` pure gameplay logic. It provides:
1. **Skeletal Animation Playback**: Multi-variant animation slots driven by Mixamo humanoid rigs.
2. **Modular Wardrobe & Equipping**: Independent modular clothing, rigid socket attachments, and full outfits.
3. **Engine-Level Selective Body Deflation**: Dynamic mesh retraction during CPU skinning to prevent clothing clipping.
4. **Verlet Spring Bone Physics**: Secondary procedural cloth simulation for robe skirts, bell sleeves, and silk ribbons.
5. **Persistent Wardrobe Collection**: Declarative JSON catalog, save/load progression, and combat stance evaluation.

---

## 3. Data Structures & Enums

### `CharacterAnimSlot`
Defines 17 action slots across locomotion, martial strikes, sword arts, casting, and hit reactions:
```c
typedef enum {
    CHAR_ANIM_IDLE, CHAR_ANIM_WALK, CHAR_ANIM_RUN,
    CHAR_ANIM_JUMP, CHAR_ANIM_JUMP_DOWN, CHAR_ANIM_DODGE,
    CHAR_ANIM_PUNCH, CHAR_ANIM_KICK, CHAR_ANIM_PALM,
    CHAR_ANIM_SWORD_IDLE, CHAR_ANIM_SWORD_SLASH, CHAR_ANIM_SWORD_THRUST,
    CHAR_ANIM_SWORD_PARRY, CHAR_ANIM_SWORD_BLOCK,
    CHAR_ANIM_CAST, CHAR_ANIM_HIT, CHAR_ANIM_KNOCKDOWN,
    CHAR_ANIM_COUNT
} CharacterAnimSlot;
```

### `ModularSlot`
Defines 13 customizable equipment and socket locations:
```c
typedef enum {
    MODULAR_SLOT_BASE_BODY = 0, // Humanoid base mesh & master rig
    MODULAR_SLOT_HAIR,          // Hair & hairpins
    MODULAR_SLOT_HEADGEAR,      // Conical hats (Nón Lá), helmets
    MODULAR_SLOT_MASK,          // Face veils, masks
    MODULAR_SLOT_TORSO,         // Robes, tunics (Áo Giao Lĩnh)
    MODULAR_SLOT_LEGS,          // Martial pants (Quần Võ Hiệp)
    MODULAR_SLOT_BOOTS,         // Boots, shoes (Chiến Hài)
    MODULAR_SLOT_BRACERS,       // Wrist bracers (Hộ Uyển)
    MODULAR_SLOT_WAIST,         // Belts, jade pendants
    MODULAR_SLOT_BACK,          // Cloaks, back scabbards (Vỏ Kiếm)
    MODULAR_SLOT_WEAPON_R,      // Right hand weapon (Thuận Thiên Kiếm)
    MODULAR_SLOT_WEAPON_L,      // Left hand weapon / shield
    MODULAR_SLOT_FULL_OUTFIT,   // Complete robe suppressing modular torso/legs
    MODULAR_SLOT_COUNT
} ModularSlot;
```

### `CharacterAnimState`
State machine tracker allocated per entity instance:
```c
typedef struct CharacterAnimState {
    int               frame;
    float             frameTimer;
    CharacterAnimSlot currentSlot;
    int               activeVariant;   // Active variant index (0..CHAR_ANIM_MAX_VARIANTS - 1)
    int               activeAnimIndex; // Index in global ModelAnimation array (-1 if none)
    bool              oneShotActive;   // true during one-shot actions
    float             oneShotSpeed;    // Playback multiplier (1.0 = normal)
} CharacterAnimState;
```

---

## 4. Invariants & Calling Contracts

1. **Scale Standard**: `1 unit = 1 meter`. Human base height is strictly **$1.78m$** (skull crown $1.76m$, bun $1.81m$).
2. **Skeleton Invariant**: Base bodies must have exactly **20 Mixamo humanoid bones** matching the engine's animation bank. Extra cloth spring bones are supported only on outer garments (28 bones) and evaluated via CPU skinning.
3. **Memory Ownership**: Models and animation clips are managed by `core/resource_manager.h`. Never call `UnloadModel()` or `UnloadModelAnimations()` directly within character state machines.
4. **Graceful Degradation**: Every function safely no-ops if `CharacterModel_IsLoaded()` is false or if an item file is missing. The engine will never crash due to a missing asset.
5. **No Direct `entities.h` Inclusion**: `character/` never includes `entities/entities.h`. The gameplay/sandbox layer maps entity combat intents to character animation slots.
