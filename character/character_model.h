// character/character_model.h
// Real 3D character model + skeletal animation (raylib Model/ModelAnimation),
// supporting multi-variant martial arts action slots, clip swapping,
// direct-by-name triggers, and Mixamo/BVH animation pipelines.
//
// Separate module from core/ — this is character-specific, not a generic engine
// primitive. Counterpart to entities/'s pure gameplay logic. Callers map their
// own attack-type enum (e.g. entities/entities.h's BasicAttackType) to
// CharacterAnimSlot themselves.
//
// Safe with no asset present: CharacterModel_Load returns false if the file
// doesn't exist (or has too few animation clips) and every other function
// becomes a silent no-op — callers must check CharacterModel_IsLoaded()
// before using this instead of procedural stick-figure fallbacks.

#ifndef CHARACTER_MODEL_H
#define CHARACTER_MODEL_H

#include "raylib.h"
#include <stdbool.h>

#define CHAR_ANIM_MAX_VARIANTS 8

typedef enum {
    // Locomotion
    CHAR_ANIM_IDLE,
    CHAR_ANIM_WALK,
    CHAR_ANIM_RUN,

    // Agility / Airborne / Mobility
    CHAR_ANIM_JUMP,
    CHAR_ANIM_JUMP_DOWN, // Landing / jumping down from height (e.g. Mixamo Jumping Down)
    CHAR_ANIM_DODGE,

    // Basic Martial Attacks
    CHAR_ANIM_PUNCH,
    CHAR_ANIM_KICK,
    CHAR_ANIM_PALM,

    // Sword Arts (Kiếm Pháp)
    CHAR_ANIM_SWORD_IDLE,
    CHAR_ANIM_SWORD_SLASH,
    CHAR_ANIM_SWORD_THRUST,
    CHAR_ANIM_SWORD_PARRY,
    CHAR_ANIM_SWORD_BLOCK,

    // Flourish / Skills
    CHAR_ANIM_CAST, // skill-cast flourish — triggered on a successful CastSkill

    // Reactions
    CHAR_ANIM_HIT,
    CHAR_ANIM_KNOCKDOWN,

    CHAR_ANIM_COUNT
} CharacterAnimSlot;

// Per-character playback state — one of these per character instance (e.g. PlayerEntity.anim).
typedef struct CharacterAnimState {
    int   frame;
    float frameTimer;
    CharacterAnimSlot currentSlot;
    int   activeVariant;    // which variant (0..count-1) of currentSlot is active
    int   activeAnimIndex;  // raw ModelAnimation index currently playing (-1 if none)
    bool  oneShotActive;    // true while an action one-shot is playing
    float oneShotSpeed;     // playback multiplier for the current one-shot (1.0 = clip's natural speed)
} CharacterAnimState;

// Loads (once, cached via core/resource_manager.h) the shared character Model + its
// animation clips from filePath (e.g. "assets/characters/player.glb").
// Scans clips and matches them to CharacterAnimSlot slots and variants.
// Also reads "assets/characters/player_anims.json" if present to override mappings.
bool CharacterModel_Load(const char *filePath);
bool CharacterModel_IsLoaded(void);

void CharacterModel_ResetState(CharacterAnimState *state);

// Advances playback. isMoving picks IDLE vs WALK/RUN when no one-shot is active;
// a one-shot plays through once and then reverts to IDLE/WALK automatically.
void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving);

// Starts a one-shot animation on a slot (variant 0 by default).
// A new trigger interrupts and replaces any one-shot still playing.
void CharacterModel_Trigger(CharacterAnimState *state, CharacterAnimSlot slot);

// Same, with custom duration scaling so the clip finishes in `seconds`.
// seconds <= 0.0f plays at the clip's natural speed.
void CharacterModel_TriggerTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds);

// Starts a specific variant of an action slot (e.g. variant 1 of SWORD_SLASH).
// If variantIndex is out of range, clamps to the closest valid variant.
void CharacterModel_TriggerVariant(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex);
void CharacterModel_TriggerVariantTimed(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex, float seconds);

// Backward-compatible wrappers for existing codebase calls:
void CharacterModel_TriggerAttack(CharacterAnimState *state, CharacterAnimSlot slot);
void CharacterModel_TriggerAttackTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds);

// Plays ANY loaded animation clip directly by name (case-insensitive substring match).
// E.g. CharacterModel_TriggerByName(state, "jumping_down", 0.0f);
// Returns true if a matching clip was found and triggered, false otherwise.
bool CharacterModel_TriggerByName(CharacterAnimState *state, const char *clipName, float seconds);

// Slot & Variant configuration & query:
int  CharacterModel_GetVariantCount(CharacterAnimSlot slot);
int  CharacterModel_GetSlotAnimIndex(CharacterAnimSlot slot, int variantIndex);
bool CharacterModel_SetSlotVariant(CharacterAnimSlot slot, int variantIndex, int animIndex);
bool CharacterModel_AddSlotVariant(CharacterAnimSlot slot, int animIndex);
void CharacterModel_ClearSlotVariants(CharacterAnimSlot slot);

// Raw animation introspection:
int         CharacterModel_GetTotalAnimCount(void);
int         CharacterModel_FindAnimIndex(const char *name);
const char* CharacterModel_GetAnimName(int animIndex);
float       CharacterModel_GetAnimDuration(int animIndex);

// Prints a diagnostic table of all loaded animations, slot mappings, and variants to TraceLog.
void CharacterModel_PrintInfo(void);

// Draws the shared Model posed per state at position, facing yawRadians (around +Y),
// scaled by `scale`. No-op if CharacterModel_IsLoaded() is false.
void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint);

// Getters for integration with visual composer / skill effects
struct MeshAdjacency;
Model CharacterModel_GetModel(void);
struct MeshAdjacency* CharacterModel_GetAdjacency(void);

// Samples a random point on the character's deformed animated mesh surface in O(1).
// Uses CPU-skinned animVertices so particles follow whatever animation is currently active.
bool CharacterModel_SampleSurfacePoint(Vector3 position, float yawRadians, float scale,
                                      Vector3 *outWorldPos, Vector3 *outWorldNormal);

#endif // CHARACTER_MODEL_H
