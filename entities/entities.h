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
