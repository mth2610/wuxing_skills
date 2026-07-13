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

// --- Hero bots (Dot A4 — nguoi choi ao filling empty team slots) ---
// A bot is an ordinary ARCH_HERO in the shared pool plus a brain here:
// keep skill range to the nearest/weakest enemy hero, cast off the agent's
// equippedSkills (the caller equips a loadout after spawning), dash away
// from incoming enemy projectiles, never walk off the arena edge. Bots run
// HOST-side only — to a connected client they are indistinguishable from
// humans (same snapshot path). They do not respawn mid-round (elimination).
#define AI_MAX_HERO_BOTS 8

// Spawns the hero agent + registers its brain. Returns the agentId (-1 on
// pool/brain-slot exhaustion). Caller equips skills afterwards.
int  AI_SpawnHeroBot(Vector3 pos, AgentTeam team);
// Kill every bot's agent + free the brains (match reset / room close).
void AI_ClearHeroBots(void);
// Living bots on a side (dead brains are swept by AI_Update).
int  AI_GetHeroBotCount(AgentTeam team);
// Is this agent one of ours? (game/ uses it to tell roster-legitimate
// heroes from leftovers when it sweeps the arena at round start.)
bool AI_IsHeroBot(int agentId);

// Bot casts this frame — drained by main.c and forwarded to the net layer
// (VFX mirroring for connected clients). Same poll idiom as explosions.
typedef struct {
    int     agentId;
    int     skillIndex;
    Vector3 aim;
} HeroBotCast;
int AI_PollHeroCasts(HeroBotCast *out, int max);

#endif // AI_H
