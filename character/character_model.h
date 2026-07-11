// character/character_model.h
// Real 3D character model + skeletal animation (raylib Model/ModelAnimation),
// as opposed to the procedural stick-figure DrawCharacter3D
// (sandbox/sandbox_core.c) it's meant to replace once a real asset exists.
//
// Separate module from core/ — this is character-specific (idle/walk/punch/
// kick/palm), not a generic engine primitive, so it doesn't belong alongside
// particle/shader/mesh utilities. Also separate from entities/ (which forbids
// rendering entirely) — this is the rendering counterpart to entities/'s pure
// gameplay logic. Deliberately does NOT include entities/entities.h (keeps
// layering clean); callers map their own attack-type enum (e.g.
// entities/entities.h's BasicAttackType) to CharacterAnimSlot themselves, one
// line per case.
//
// Safe with no asset present: CharacterModel_Load returns false if the file
// doesn't exist (or has too few animation clips) and every other function
// becomes a silent no-op — callers must check CharacterModel_IsLoaded()
// before using this instead of the old stick-figure fallback.
#ifndef CHARACTER_MODEL_H
#define CHARACTER_MODEL_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    CHAR_ANIM_IDLE,
    CHAR_ANIM_WALK,
    CHAR_ANIM_PUNCH,
    CHAR_ANIM_KICK,
    CHAR_ANIM_PALM,
    CHAR_ANIM_CAST, // skill-cast flourish — triggered on a successful CastSkill
    CHAR_ANIM_COUNT
} CharacterAnimSlot;

// Per-character playback state — one of these per character instance (e.g.
// PlayerEntity.anim). Multiple instances must NOT share one CharacterModel_Load
// call's Model and animate independently at the same time: UpdateModelAnimation
// mutates the Model's mesh buffer in place (CPU skinning), so two instances
// drawn with different frames in the same frame would clobber each other.
// Only the player uses this in this pass — flagging this for whoever wires up
// a second animated instance later.
typedef struct {
    int   frame;
    float frameTimer;
    CharacterAnimSlot currentSlot;
    bool  oneShotActive; // true while a PUNCH/KICK/PALM/CAST one-shot is playing
    float oneShotSpeed;  // playback multiplier for the current one-shot (1.0 = clip's natural speed)
} CharacterAnimState;

// Loads (once, cached via core/resource_manager.h) the shared character
// Model + its animation clips from filePath. Returns false if the file
// doesn't exist or has no usable animations — caller should keep using the
// old procedural DrawCharacter3D in that case. Matches clip names to
// CHAR_ANIM_* slots by case-insensitive substring ("idle", "walk", "punch",
// "kick" — palm reuses whatever's left/closest, see .c) and logs via
// TraceLog which slots resolved so real clip names can be checked/adjusted
// once a file exists.
bool CharacterModel_Load(const char *filePath);
bool CharacterModel_IsLoaded(void);

void CharacterModel_ResetState(CharacterAnimState *state);

// Advances playback. isMoving picks IDLE vs WALK when no one-shot is active;
// a one-shot (from CharacterModel_TriggerAttack) plays through once and then
// reverts to IDLE/WALK automatically.
void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving);

// Starts a one-shot PUNCH/KICK/PALM/CAST animation (ignored if that slot has
// no matched clip). A new trigger interrupts and replaces any one-shot still
// playing — basic attacks are spammable, so a fresh press must restart the
// swing instead of being silently eaten by a longer clip (e.g. the 4.2s
// casting flourish) that hasn't finished. Plays at the clip's natural speed.
void CharacterModel_TriggerAttack(CharacterAnimState *state, CharacterAnimSlot slot);

// Same, but compresses/stretches playback so the whole clip finishes in
// `seconds` — playback speed is derived per call, NOT from a fixed table,
// because how long a move should take depends on the move: each skill
// supplies its own flourish duration (core/skill_manager.h's
// Skill_GetCastAnimSeconds), a heavy palm strike takes longer than a jab.
// seconds <= 0 falls back to natural speed.
void CharacterModel_TriggerAttackTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds);

// Draws the shared Model posed per state at position, facing yawRadians
// (rotation around +Y), uniformly scaled by `scale` (expect to need tuning
// once a real asset exists — Mixamo/Blender export scale is a common
// mismatch source, see root CLAUDE.md's meter-rescale history). No-op if
// CharacterModel_IsLoaded() is false.
void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint);

#endif // CHARACTER_MODEL_H
