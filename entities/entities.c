// entities/entities.c
#include "entities.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

static bool Entity_ProvideAgentPos(int agentId, Vector3 *outPos) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a) return false;
    *outPos = a->position;
    return true;
}

static Agent agentPool[MAX_AGENTS]; // 4 ally + 4 enemy AI

// Arena constants — must match MAP_API.md §3 exactly.
// Real-world-scaled: 1 unit = 1 meter (was 1 unit = 1cm before the rescale).
static const Vector3 ARENA_CENTER = { 6.0f, 0.0f, 4.4f };
static const float   ARENA_RADIUS = 18.0f;
static const float   GRAVITY = 5.0f; // below real 9.81 m/s² by design (floatier game feel)
static const float   RING_OUT_KILL_Y = -2.0f;
static const float   DEFAULT_MAX_MANA = 100.0f;
static const float   MANA_REGEN_PER_SEC = 5.0f;
// Basic attack tuning (see Entity_ExecuteBasicAttack). Real-world-scaled —
// see root CLAUDE.md scale rules; melee range/wall-check radius sized to
// "arm's reach" and "standing next to your own wall" respectively.
static const float   BASIC_ATTACK_RANGE = 1.5f;
static const float   WALL_CHECK_RADIUS = 3.0f;
// Auto-target search radius — how far Entity_ExecuteBasicAttack looks for
// the nearest other agent, no explicit target needed.
static const float   AUTO_TARGET_RADIUS = 10.0f;

void Entity_Init(void) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        agentPool[i] = (Agent){ 0 };
    }
    SkillManager_SetAgentPosProvider(Entity_ProvideAgentPos);
    SkillManager_SetNearbyTargetsProvider(Entity_GetNearbyTargets);
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

        if (a->stunTimer > 0.0f) {
            a->stunTimer -= dt;
            if (a->stunTimer < 0.0f) a->stunTimer = 0.0f;
        }

        if (a->mana < a->maxMana) {
            a->mana += MANA_REGEN_PER_SEC * dt;
            if (a->mana > a->maxMana) a->mana = a->maxMana;
        }

        if (a->pullTimer > 0.0f) {
            float dx = a->pullTarget.x - a->position.x;
            float dz = a->pullTarget.z - a->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            float step = a->pullSpeed * dt;
            if (dist <= step || dist < 0.0001f) {
                a->position.x = a->pullTarget.x;
                a->position.z = a->pullTarget.z;
            } else {
                a->position.x += (dx / dist) * step;
                a->position.z += (dz / dist) * step;
            }
            a->pullTimer -= dt;
            if (a->pullTimer < 0.0f) a->pullTimer = 0.0f;
        }

        Entity_CheckRingOut(i);

        if (a->vState == AGENT_RING_OUT_FALLING) {
            a->velocity.y -= GRAVITY * dt;
            a->position.y += a->velocity.y * dt;
            if (a->position.y < RING_OUT_KILL_Y) {
                a->active = false;
            }
        } else if (a->vState == AGENT_JUMPING) {
            a->velocity.y -= GRAVITY * dt;
            a->position.x += a->velocity.x * dt;
            a->position.y += a->velocity.y * dt;
            a->position.z += a->velocity.z * dt;
            if (a->position.y <= 0.0f) {
                a->position.y = 0.0f;
                a->velocity = (Vector3){ 0 };
                a->vState = AGENT_GROUNDED;
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

void Entity_ApplyLaunch(int agentId, float verticalForce, Vector3 horizontalVelocity) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->velocity = (Vector3){ horizontalVelocity.x, verticalForce, horizontalVelocity.z };
    a->vState = AGENT_JUMPING;
}

void Entity_ApplyStun(int agentId, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->stunTimer = duration;
}

bool Entity_IsStunned(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    Agent *a = &agentPool[agentId];
    if (!a->active) return false;
    return a->stunTimer > 0.0f;
}

void Entity_ApplyPull(int agentId, Vector3 targetPos, float speed, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->pullTarget = targetPos;
    a->pullSpeed = speed;
    a->pullTimer = duration;
}

bool Entity_IsCrowdControlled(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    Agent *a = &agentPool[agentId];
    if (!a->active) return false;
    return a->stunTimer > 0.0f || a->vState != AGENT_GROUNDED || a->pullTimer > 0.0f;
}

bool Entity_TrySpendMana(int agentId, float amount) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    Agent *a = &agentPool[agentId];
    if (!a->active) return false;
    if (a->mana < amount) return false;
    a->mana -= amount;
    return true;
}

float Entity_GetBasicAttackSeconds(BasicAttackType type) {
    switch (type) {
        case BASIC_ATTACK_PUNCH: return 1.0f;
        case BASIC_ATTACK_KICK:  return 1.2f;
        case BASIC_ATTACK_PALM:  return 1.4f;
        default:                 return 1.0f;
    }
}

bool Entity_ExecuteBasicAttack(int attackerId, BasicAttackType type,
                               Vector3 *outTargetPos,
                               Vector3 *outWallPos, int *outWallElement) {
    if (outTargetPos) *outTargetPos = (Vector3){ 0 };
    if (outWallPos) *outWallPos = (Vector3){ 0 };
    if (outWallElement) *outWallElement = -1;

    if (attackerId < 0 || attackerId >= MAX_AGENTS) return false;
    Agent *attacker = &agentPool[attackerId];
    if (!attacker->active) return false;

    // Auto-target: nearest other active agent within AUTO_TARGET_RADIUS — no
    // targetId parameter, no "enemy" reference needed. No team filtering yet
    // (Agent has no team field — known gap, tracked for Module 1).
    int nearbyIds[MAX_AGENTS];
    int nearbyCount = Entity_GetNearbyTargets(attacker->position, AUTO_TARGET_RADIUS, nearbyIds, MAX_AGENTS);
    int targetId = -1;
    float bestDistSq = 0.0f;
    for (int i = 0; i < nearbyCount; i++) {
        if (nearbyIds[i] == attackerId) continue;
        Agent *candidate = &agentPool[nearbyIds[i]];
        float cdx = candidate->position.x - attacker->position.x;
        float cdz = candidate->position.z - attacker->position.z;
        float cDistSq = cdx * cdx + cdz * cdz;
        if (targetId == -1 || cDistSq < bestDistSq) {
            targetId = nearbyIds[i];
            bestDistSq = cDistSq;
        }
    }
    if (targetId == -1) return false;

    Agent *target = &agentPool[targetId];
    if (outTargetPos) *outTargetPos = target->position;

    float dx = target->position.x - attacker->position.x;
    float dz = target->position.z - attacker->position.z;
    float distSq = dx * dx + dz * dz;

    float dist = sqrtf(distSq);
    Vector3 pushDir = { 0.0f, 0.0f, 0.0f };
    if (dist > 0.0001f) {
        pushDir.x = dx / dist;
        pushDir.z = dz / dist;
    }

    // Melee hit only within BASIC_ATTACK_RANGE — but the wall-bonus check
    // below is intentionally NOT gated behind this: the whole point of the
    // wall synergy is a free ranged substitute when the enemy is too far to
    // melee, so a wall bonus must still be able to fire even if the base
    // punch/kick/palm doesn't land.
    if (distSq <= BASIC_ATTACK_RANGE * BASIC_ATTACK_RANGE) {
        switch (type) {
            case BASIC_ATTACK_PUNCH:
                Entity_ApplyDamage(targetId, 5.0f, Vector3Scale(pushDir, 1.5f));
                break;
            case BASIC_ATTACK_KICK:
                Entity_ApplyDamage(targetId, 8.0f, Vector3Scale(pushDir, 3.0f));
                break;
            case BASIC_ATTACK_PALM:
                // "Chưởng" — highest damage, breaks guard, small pop into the air.
                Entity_ApplyDamage(targetId, 12.0f, (Vector3){ 0 });
                Entity_ApplyLaunch(targetId, 3.0f, Vector3Scale(pushDir, 2.0f));
                break;
        }
    }

    Vector3 wallPos;
    int wallElement;
    if (SkillManager_FindNearbyWall(attacker->position, WALL_CHECK_RADIUS, &wallPos, &wallElement)) {
        // Free elemental bonus hit — no mana cost, this whole path never
        // touches Entity_TrySpendMana. Applies regardless of melee range
        // (the rock/bolt is what closes the distance, not the fist).
        Entity_ApplyDamage(targetId, 6.0f, Vector3Scale(pushDir, 2.0f));
        if (outWallPos) *outWallPos = wallPos;
        if (outWallElement) *outWallElement = wallElement;
        return true;
    }

    return false;
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
            a->stunTimer = 0.0f;
            a->pullTarget = (Vector3){ 0 };
            a->pullTimer = 0.0f;
            a->pullSpeed = 0.0f;
            a->maxMana = DEFAULT_MAX_MANA;
            a->mana = DEFAULT_MAX_MANA;
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
