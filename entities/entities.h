// entities/entities.h
// Entities module — Agent pool, vertical physics (jump/dash/ring-out),
// damage entry point. See ENTITIES_API.md for full spec.
#ifndef ENTITIES_H
#define ENTITIES_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    AGENT_GROUNDED,
    AGENT_JUMPING,
    AGENT_RING_OUT_FALLING
} AgentVerticalState;

#define MAX_AGENT_MODIFIERS 4

// Minimal duration-based modifier slot (e.g. Buff speed multiplier).
// speedMult: 1.0 = normal speed; multiplicative; <=0 or unset slot ignored.
// duration: seconds remaining; <=0 means inactive/empty slot.
// NOTE: speedMult is NOT wired into any movement code yet — there is no
// movement system in this minimal version. This is data + tick-down only.
typedef struct {
    float speedMult;
    float duration;
} AgentModifier;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float   health;
    float   maxHealth;
    int     currentElement;      // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    AgentVerticalState vState;
    float   dashCooldown;        // seconds remaining
    bool    isStealthed;         // reserved, not consumed yet
    bool    active;
    AgentModifier modifiers[MAX_AGENT_MODIFIERS];

    // --- Crowd control (see §12 in ENTITIES_API.md) ---
    float   stunTimer;   // seconds remaining; <=0 = not stunned. Orthogonal to
                          // vState — an agent can be airborne AND stunned.
                          // Entities only ticks this down; enforcing "can't
                          // act while stunned" is the caller's job.
    Vector3 pullTarget;   // world position being pulled toward
    float   pullTimer;    // seconds remaining; <=0 = not being pulled
    float   pullSpeed;    // units/sec toward pullTarget, XZ-plane only

    // --- Mana (see §13 in ENTITIES_API.md) ---
    float   mana;
    float   maxMana;
} Agent;

// Sized for the real target scale (up to 6 real players + a mixed pool of
// minions/mid-tier monsters/2 bosses), not just the original 4v4 test
// scaffolding — static array, cost is ~20KB total, negligible. Bump further
// if a future design ever needs more; this is a compile-time ceiling only,
// actual live agent count still varies freely under it at runtime.
#define MAX_AGENTS 256

// --- Lifecycle ---
void Entity_Init(void);
void Entity_Update(float dt);
void Entity_Draw(void);
void Entity_Unload(void);

// --- Damage ---
void Entity_ApplyDamage(int agentId, float damage, Vector3 knockback);

// --- Vertical physics ---
void Entity_Jump(int agentId, float force);
void Entity_Dash(int agentId, Vector3 direction, float speed);
bool Entity_CheckRingOut(int agentId);

// --- Crowd control (see ENTITIES_API.md §12) ---
// Launch: sets velocity and vState = AGENT_JUMPING (shared with Entity_Jump).
// Entity_Update integrates gravity + full 3-axis position while airborne and
// lands back to AGENT_GROUNDED at position.y <= 0 — a real ballistic arc, not
// just a vertical pop.
void Entity_ApplyLaunch(int agentId, float verticalForce, Vector3 horizontalVelocity);
// Stun: sets/refreshes stunTimer. Entities has no action/input system to
// enforce this — callers (skill cast, input) must check Entity_IsStunned.
void Entity_ApplyStun(int agentId, float duration);
bool Entity_IsStunned(int agentId);
// Pull: entities directly integrates position.x/z toward targetPos at speed
// for duration seconds (self-contained, does not rely on velocity/external
// integration — see ENTITIES_API.md §12 for why).
void Entity_ApplyPull(int agentId, Vector3 targetPos, float speed, float duration);
// Convenience combined read for external movement/input systems deciding
// whether to skip their own position writes this frame.
bool Entity_IsCrowdControlled(int agentId);

// --- Mana (see ENTITIES_API.md §13) ---
// Spawned agents get mana = maxMana = 100.0f by default (Entity_SpawnAgent).
// Passive regen (5/sec) ticks in Entity_Update, capped at maxMana.
// Returns false and leaves mana untouched if amount > current mana.
bool Entity_TrySpendMana(int agentId, float amount);

// --- Basic attack (see ENTITIES_API.md §13) ---
// Separate from the CastSkill/skill pipeline entirely — free, no cast time,
// no cooldown gate, spammable. Auto-targets: internally scans
// Entity_GetNearbyTargets around the attacker (no targetId parameter — no
// "enemy" reference needed) and picks the nearest active agent other than
// the attacker. No team filtering yet (Agent has no team field — known gap,
// see entities/CLAUDE.md's Module 1 note). Returns false immediately with no
// side effects if no other agent is within AUTO_TARGET_RADIUS.
// Applies damage/knockback on the auto-found target (PALM also applies a
// small Entity_ApplyLaunch pop) and reports its position via outTargetPos
// (caller has no targetId to look this up itself — needed for VFX, e.g. a
// wall-bonus beam's endpoint). If the attacker is standing near a
// registered wall (core/skill_manager.h's SkillManager_FindNearbyWall), also
// applies a small bonus hit and reports the wall's position/element via
// out-params so the caller (which owns VFX — entities does not call
// rendering code) can spawn the matching elemental bonus effect. Returns
// true iff a wall bonus fired (independent of whether a target was found —
// wall bonus still needs a target to hit, see entities.c).
typedef enum { BASIC_ATTACK_PUNCH, BASIC_ATTACK_KICK, BASIC_ATTACK_PALM } BasicAttackType;
bool Entity_ExecuteBasicAttack(int attackerId, BasicAttackType type,
                               Vector3 *outTargetPos,
                               Vector3 *outWallPos, int *outWallElement);

// How long this attack's swing takes, in seconds — gameplay data (single
// source of truth for both game/ and sandbox/ callers), consumed by the
// rendering side (character/character_model.h's
// CharacterModel_TriggerAttackTimed) to pace the swing animation. Escalates
// with the move's weight: punch < kick < palm.
float Entity_GetBasicAttackSeconds(BasicAttackType type);

// --- Dash/afterimage hook (stub) ---
void Entity_OnDash(int agentId);

// --- Nearby target query (pure read, no side effects) ---
// Returns the number of active agents found within radius of center, writing
// their indices into outIds (caller-supplied buffer, size maxIds).
// XZ-plane distance check (ignore Y), matching arena/ring-out checks.
int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds);

// --- Buff modifier setter ---
// Finds an empty/expired slot in agentPool[agentId].modifiers[] and writes
// into it (simple find-first-empty, no priority/stacking logic).
void Entity_AddModifier(int agentId, float speedMult, float duration);

// --- AoE composition entry points (built on Entity_GetNearbyTargets) ---
// Applies damage + knockback (away from center) to every active agent found
// within radius. NO team/ally-enemy filtering (Agent has no team field yet).
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage, float knockbackStrength);
// Applies a buff modifier to every active agent found within radius.
// WARNING: NO team/ally-enemy filtering yet — this buffs ALL agents in
// radius, friend or foe alike. See ENTITIES_API.md for details.
void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult, float duration);

// --- Spawn / read-only access ---
// Finds the first inactive slot in agentPool, initializes it, returns its id
// (0..MAX_AGENTS-1), or -1 if the pool is full.
int Entity_SpawnAgent(Vector3 position, float maxHealth, int element);

// Read-only accessor. Returns NULL if agentId is out of range or the slot is
// inactive — caller must NULL-check, do not assume a valid pointer. This is
// the only sanctioned way for other modules to read agent state; mutation
// must go through the existing Entity_* setter functions.
const Agent *Entity_GetAgent(int agentId);

// --- Position sync (for external movement systems) ---
// Overwrites agentId's position directly. Entities module owns no horizontal
// movement system — callers (e.g. sandbox/skills) that compute their own
// X/Z movement must push the result here each frame so Entity_GetNearbyTargets
// and other position-based queries stay accurate. Does nothing if agentId is
// out of range or inactive.
void Entity_SetPosition(int agentId, Vector3 position);

#endif // ENTITIES_H
