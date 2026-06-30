// entities/entities.c
#include "entities.h"
#include <math.h>
#include <stddef.h>

static Agent agentPool[MAX_AGENTS]; // 4 ally + 4 enemy AI

// Arena constants — must match MAP_API.md §3 exactly.
static const Vector3 ARENA_CENTER = { 600.0f, 0.0f, 440.0f };
static const float   ARENA_RADIUS = 1800.0f;
static const float   GRAVITY = 500.0f;
static const float   RING_OUT_KILL_Y = -200.0f;

void Entity_Init(void) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        agentPool[i] = (Agent){ 0 };
    }
}

void Entity_Update(float dt) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        Agent *a = &agentPool[i];
        if (!a->active) continue;

        // Tick down modifier slots.
        for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
            if (a->modifiers[m].duration > 0.0f) {
                a->modifiers[m].duration -= dt;
                if (a->modifiers[m].duration <= 0.0f) {
                    a->modifiers[m].duration = 0.0f;
                    a->modifiers[m].speedMult = 0.0f;
                }
            }
        }

        if (a->dashCooldown > 0.0f) {
            a->dashCooldown -= dt;
            if (a->dashCooldown < 0.0f) a->dashCooldown = 0.0f;
        }

        Entity_CheckRingOut(i);

        if (a->vState == AGENT_RING_OUT_FALLING) {
            a->velocity.y -= GRAVITY * dt;
            a->position.y += a->velocity.y * dt;
            if (a->position.y < RING_OUT_KILL_Y) {
                a->active = false;
            }
        }
    }
}

void Entity_Draw(void) {
    // stub — entities are visually represented by skills/billboards owned elsewhere
}

void Entity_Unload(void) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        agentPool[i] = (Agent){ 0 };
    }
}

void Entity_ApplyDamage(int agentId, float damage, Vector3 knockback) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;

    a->health -= damage;
    a->velocity.x += knockback.x;
    a->velocity.y += knockback.y;
    a->velocity.z += knockback.z;

    if (a->health <= 0.0f) {
        a->health = 0.0f;
        a->active = false;
    }
}

void Entity_Jump(int agentId, float force) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (a->vState == AGENT_GROUNDED) {
        a->velocity.y = force;
        a->vState = AGENT_JUMPING;
    }
}

void Entity_Dash(int agentId, Vector3 direction, float speed) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    (void)direction;
    (void)speed;
    a->dashCooldown = 1.0f; // placeholder cooldown value; real tuning TBD
    Entity_OnDash(agentId);
}

bool Entity_CheckRingOut(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    Agent *a = &agentPool[agentId];
    if (!a->active) return false;

    float dx = a->position.x - ARENA_CENTER.x;
    float dz = a->position.z - ARENA_CENTER.z;
    float distSq = dx * dx + dz * dz;

    if (distSq > ARENA_RADIUS * ARENA_RADIUS) {
        if (a->vState != AGENT_RING_OUT_FALLING) {
            a->vState = AGENT_RING_OUT_FALLING;
        }
        return true;
    }
    return false;
}

void Entity_OnDash(int agentId) {
    (void)agentId;
    // reserved hook — no VFX wiring in this module
}

int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds) {
    if (outIds == NULL || maxIds <= 0) return 0;

    int count = 0;
    float radiusSq = radius * radius;

    for (int i = 0; i < MAX_AGENTS && count < maxIds; i++) {
        Agent *a = &agentPool[i];
        if (!a->active) continue;

        float dx = a->position.x - center.x;
        float dz = a->position.z - center.z;
        float distSq = dx * dx + dz * dz;

        if (distSq <= radiusSq) {
            outIds[count++] = i;
        }
    }

    return count;
}

void Entity_AddModifier(int agentId, float speedMult, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;

    for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
        if (a->modifiers[m].duration <= 0.0f) {
            a->modifiers[m].speedMult = speedMult;
            a->modifiers[m].duration = duration;
            return;
        }
    }
    // no empty slot found — silently drop (minimal version, no eviction policy)
}

void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage, float knockbackStrength) {
    int ids[MAX_AGENTS];
    int count = Entity_GetNearbyTargets(center, radius, ids, MAX_AGENTS);

    for (int i = 0; i < count; i++) {
        int id = ids[i];
        Agent *a = &agentPool[id];

        // XZ-plane direction away from center, consistent with the distance
        // check in Entity_GetNearbyTargets (Y ignored).
        float dx = a->position.x - center.x;
        float dz = a->position.z - center.z;
        float len = sqrtf(dx * dx + dz * dz);

        Vector3 knockback = { 0.0f, 0.0f, 0.0f };
        if (len > 0.0001f) {
            knockback.x = (dx / len) * knockbackStrength;
            knockback.z = (dz / len) * knockbackStrength;
        }

        Entity_ApplyDamage(id, damage, knockback);
    }
}

void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult, float duration) {
    int ids[MAX_AGENTS];
    int count = Entity_GetNearbyTargets(center, radius, ids, MAX_AGENTS);

    // NOTE: no team/ally-enemy filtering — Agent has no team field yet.
    // This buffs every active agent found in radius, friend or foe.
    for (int i = 0; i < count; i++) {
        Entity_AddModifier(ids[i], speedMult, duration);
    }
}

int Entity_SpawnAgent(Vector3 position, float maxHealth, int element) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agentPool[i].active) {
            Agent *a = &agentPool[i];
            a->position = position;
            a->velocity = (Vector3){ 0 };
            a->health = maxHealth;
            a->maxHealth = maxHealth;
            a->currentElement = element;
            a->vState = AGENT_GROUNDED;
            a->dashCooldown = 0.0f;
            a->isStealthed = false;
            a->active = true;
            for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
                a->modifiers[m] = (AgentModifier){ 0 };
            }
            return i;
        }
    }
    return -1;
}

void Entity_SetPosition(int agentId, Vector3 position) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    if (!agentPool[agentId].active) return;
    agentPool[agentId].position = position;
}

const Agent *Entity_GetAgent(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return NULL;
    if (!agentPool[agentId].active) return NULL;
    return &agentPool[agentId];
}
