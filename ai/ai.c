// ai/ai.c — Module 8: minion steering + self-destruct. Stateless brain: all
// persistent state lives on the Agent itself; this module just walks the
// pool every frame (same iteration idiom as Entity_Update).
#include "ai/ai.h"
#include <math.h>
#include <stddef.h>

// Meter-scaled (1 unit = 1m).
static const float MINION_SPEED_MPS     = 2.0f;
static const float MINION_TRIGGER_RANGE = 1.2f;  // detonate within this
static const float MINION_BLAST_RADIUS  = 2.0f;
static const float MINION_BLAST_DAMAGE  = 15.0f;
static const float MINION_BLAST_KB      = 3.0f;
static const float MINION_HP            = 20.0f;
static const float WAVE_RING_RADIUS     = 1.6f;  // spawn ring around the boss

#define MAX_MINION_EXPLOSIONS 32
static MinionExplosion s_explosions[MAX_MINION_EXPLOSIONS];
static int s_explosionCount = 0;

void AI_Init(void) {
    s_explosionCount = 0;
}

// Nearest opposing agent; prefers ARCH_BOSS ("lầm lũi về boss địch"), falls
// back to any opposing agent when no enemy boss lives.
static int FindMarchTarget(const Agent *self) {
    int best = -1, bestBoss = -1;
    float bestSq = 0.0f, bestBossSq = 0.0f;
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->team == self->team) continue;
        float dx = a->position.x - self->position.x;
        float dz = a->position.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (best == -1 || dSq < bestSq) { best = i; bestSq = dSq; }
        if (a->archetype == ARCH_BOSS && (bestBoss == -1 || dSq < bestBossSq)) {
            bestBoss = i; bestBossSq = dSq;
        }
    }
    return (bestBoss != -1) ? bestBoss : best;
}

void AI_Update(float dt) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *m = Entity_GetAgent(i);
        if (!m || m->archetype != ARCH_MINION) continue;
        if (m->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(i)) continue;

        int targetId = FindMarchTarget(m);
        if (targetId < 0) continue;
        const Agent *target = Entity_GetAgent(targetId);

        float dx = target->position.x - m->position.x;
        float dz = target->position.z - m->position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist <= MINION_TRIGGER_RANGE) {
            // Self-destruct: team-aware AoE, event for the VFX layer, die.
            Vector3 at = m->position;
            int elem = m->currentElement;
            AgentTeam team = m->team;
            if (s_explosionCount < MAX_MINION_EXPLOSIONS) {
                s_explosions[s_explosionCount++] = (MinionExplosion){ at, elem };
            }
            Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 }); // the minion goes first
            Entity_ApplyAoEDamage(at, MINION_BLAST_RADIUS, MINION_BLAST_DAMAGE,
                                  MINION_BLAST_KB, team);
            continue;
        }

        // Steady march (speedMult-aware like any external mover).
        float speed = MINION_SPEED_MPS * Entity_GetSpeedMult(i);
        Vector3 pos = m->position;
        pos.x += (dx / dist) * speed * dt;
        pos.z += (dz / dist) * speed * dt;
        Entity_SetPosition(i, pos);
    }
}

int AI_SpawnMinionWave(int bossAgentId, int count) {
    const Agent *boss = Entity_GetAgent(bossAgentId);
    if (boss == NULL || count <= 0) return 0;

    int spawned = 0;
    for (int k = 0; k < count; k++) {
        float ang = ((float)k / (float)count) * 2.0f * 3.14159265f;
        Vector3 pos = { boss->position.x + cosf(ang) * WAVE_RING_RADIUS, 0.0f,
                        boss->position.z + sinf(ang) * WAVE_RING_RADIUS };
        int id = Entity_SpawnAgent(pos, MINION_HP, boss->currentElement,
                                   boss->team, ARCH_MINION);
        if (id < 0) break; // pool full
        spawned++;
    }
    return spawned;
}

int AI_PollExplosions(MinionExplosion *out, int max) {
    if (out == NULL || max <= 0) return 0;
    int n = (s_explosionCount < max) ? s_explosionCount : max;
    for (int i = 0; i < n; i++) out[i] = s_explosions[i];
    s_explosionCount = 0;
    return n;
}
