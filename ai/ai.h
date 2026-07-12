// ai/ai.h
// Minion brain (MODULES_ROADMAP.md Module 8). Minions are ordinary
// ARCH_MINION agents in the shared entities pool — this module only adds
// BEHAVIOR: steady march toward the enemy boss (fallback: nearest enemy),
// self-destruct AoE on contact. Pure logic — no VFX includes; explosions are
// reported as poll events (same philosophy as Entity_OnDash) for the render
// side (game/main) to compose. AI_API.md will document the contract.
#ifndef AI_H
#define AI_H

#include "entities/entities.h"

void AI_Init(void);

// Walks every active ARCH_MINION: steer toward the opposing team's boss
// (nearest opposing agent when no boss lives), explode within trigger range
// — team-aware Entity_ApplyAoEDamage + a poll event, then the minion dies.
void AI_Update(float dt);

// Spawn `count` minions in a ring around the boss agent, inheriting its
// team and current element. Returns how many actually spawned (pool caps).
int AI_SpawnMinionWave(int bossAgentId, int count);

// Explosion events for the VFX layer — drained per frame by the consumer.
typedef struct {
    Vector3 pos;
    int     element; // minion's element at detonation (VFX preset pick)
} MinionExplosion;
int AI_PollExplosions(MinionExplosion *out, int max);

#endif // AI_H
