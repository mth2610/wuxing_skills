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
// Thiền Định: 3s immobile channel; regen sized so an empty default pool
// (100) refills within the 3s window (34 * 3 > 100).
static const float   MEDITATE_DURATION = 3.0f;
static const float   MEDITATE_REGEN_PER_SEC = 34.0f;
// Dash: short horizontal burst, then cooldown gate.
static const float   DASH_DURATION = 0.15f;
static const float   DASH_COOLDOWN = 1.0f;
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

        // Thái Cực "Vô Sát" downside: the state lives on mana — a dry pool
        // exits it (Lôi's huge cost is the intended drain). Checked BEFORE
        // regen so the tick that drained to zero actually exits.
        if (a->taijiActive && a->mana <= 0.01f) {
            a->taijiActive = false;
        }

        if (a->mana < a->maxMana) {
            float regen = a->isMeditating ? MEDITATE_REGEN_PER_SEC : MANA_REGEN_PER_SEC;
            a->mana += regen * dt;
            if (a->mana > a->maxMana) a->mana = a->maxMana;
        }

        if (a->isMeditating) {
            a->meditateTimer -= dt;
            if (a->meditateTimer <= 0.0f) {
                a->meditateTimer = 0.0f;
                a->isMeditating = false;
            }
        }

        if (a->dashTimer > 0.0f) {
            // Clamp the final step to the remaining burst time so total
            // distance is exactly speed * DASH_DURATION (no float-residue
            // extra frame).
            float step = (dt < a->dashTimer) ? dt : a->dashTimer;
            a->position.x += a->dashVelocity.x * step;
            a->position.z += a->dashVelocity.z * step;
            a->dashTimer -= dt;
            if (a->dashTimer <= 0.0f) {
                a->dashTimer = 0.0f;
                a->dashVelocity = (Vector3){ 0 };
            }
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

// Any hostile/forced action breaks Thiền Định.
static void CancelMeditate(Agent *a) {
    a->isMeditating = false;
    a->meditateTimer = 0.0f;
}

void Entity_ApplyDamage(int agentId, float damage, Vector3 knockback) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;

    CancelMeditate(a);
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
        CancelMeditate(a);
        a->velocity.y = force;
        a->vState = AGENT_JUMPING;
    }
}

void Entity_Dash(int agentId, Vector3 direction, float speed) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (a->dashCooldown > 0.0f) return;

    // XZ-plane burst — normalize the direction, ignore Y.
    float len = sqrtf(direction.x * direction.x + direction.z * direction.z);
    if (len < 0.0001f) return;

    CancelMeditate(a);
    a->dashVelocity = (Vector3){ (direction.x / len) * speed, 0.0f,
                                 (direction.z / len) * speed };
    a->dashTimer = DASH_DURATION;
    a->dashCooldown = DASH_COOLDOWN;
    Entity_OnDash(agentId);
}

void Entity_ApplyLaunch(int agentId, float verticalForce, Vector3 horizontalVelocity) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    CancelMeditate(a);
    a->velocity = (Vector3){ horizontalVelocity.x, verticalForce, horizontalVelocity.z };
    a->vState = AGENT_JUMPING;
}

void Entity_ApplyStun(int agentId, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    CancelMeditate(a);
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
    CancelMeditate(a);
    a->pullTarget = targetPos;
    a->pullSpeed = speed;
    a->pullTimer = duration;
}

void Entity_StartMeditate(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (a->vState != AGENT_GROUNDED) return;
    if (a->stunTimer > 0.0f || a->pullTimer > 0.0f) return;
    a->isMeditating = true;
    a->meditateTimer = MEDITATE_DURATION;
}

bool Entity_IsMeditating(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a != NULL && a->isMeditating;
}

void Entity_SetEquippedSkill(int agentId, int slot, int skillId, int element) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    if (slot < 0 || slot >= AGENT_SKILL_SLOTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->equippedSkills[slot] = skillId;
    a->equippedElements[slot] = (skillId >= 0) ? element : -1;
    Entity_RecomputeElement(agentId);
}

int Entity_RecomputeElement(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return -1;
    Agent *a = &agentPool[agentId];
    if (!a->active) return -1;

    int counts[5] = { 0 };
    int filled = 0;
    for (int s = 0; s < AGENT_SKILL_SLOTS; s++) {
        if (a->equippedSkills[s] < 0) continue;
        int e = a->equippedElements[s];
        if (e < 0 || e > 4) continue;
        counts[e]++;
        filled++;
    }
    if (filled == 0) return a->currentElement; // empty loadout — keep as-is

    // Thái Cực trigger (thiết kế Trụ cột 3): balanced 2 Âm (Thủy 0 / Mộc 1)
    // + 2 Dương (Hỏa 2 / Kim 4) loadout enters the state; any other full
    // loadout leaves it (boss/'s forced flag is reapplied by Boss_Update).
    int yin  = counts[0] + counts[1];
    int yang = counts[2] + counts[4];
    a->taijiActive = (filled == AGENT_SKILL_SLOTS && yin == 2 && yang == 2);

    int best = 0;
    for (int e = 1; e < 5; e++) {
        if (counts[e] > counts[best]) best = e; // tie → lowest index wins
    }
    a->currentElement = best;
    return best;
}

void Entity_SetTaijiActive(int agentId, bool active) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->taijiActive = active;
}

bool Entity_IsTaijiActive(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a != NULL && a->taijiActive;
}

void Entity_SetStealth(int agentId, bool stealthed) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->isStealthed = stealthed;
}

void Entity_SetElement(int agentId, int element) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (element < 0 || element > 4) return;
    a->currentElement = element;
}

float Entity_GetSpeedMult(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a) return 1.0f;
    float mult = 1.0f;
    for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
        if (a->modifiers[m].duration > 0.0f && a->modifiers[m].speedMult > 0.0f) {
            mult *= a->modifiers[m].speedMult;
        }
    }
    return mult;
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

    // Auto-target: nearest active agent within AUTO_TARGET_RADIUS on a
    // different team — no targetId parameter, no "enemy" reference needed.
    int nearbyIds[MAX_AGENTS];
    int nearbyCount = Entity_GetNearbyTargets(attacker->position, AUTO_TARGET_RADIUS, nearbyIds, MAX_AGENTS);
    int targetId = -1;
    float bestDistSq = 0.0f;
    for (int i = 0; i < nearbyCount; i++) {
        if (nearbyIds[i] == attackerId) continue;
        Agent *candidate = &agentPool[nearbyIds[i]];
        if (candidate->team == attacker->team) continue;
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

int Entity_GetNearbyTargetsTeam(Vector3 center, float radius, AgentTeam filter,
                                int *outIds, int maxIds) {
    if (outIds == NULL || maxIds <= 0) return 0;

    int count = 0;
    float radiusSq = radius * radius;

    for (int i = 0; i < MAX_AGENTS && count < maxIds; i++) {
        Agent *a = &agentPool[i];
        if (!a->active || a->team != filter) continue;

        float dx = a->position.x - center.x;
        float dz = a->position.z - center.z;
        if (dx * dx + dz * dz <= radiusSq) {
            outIds[count++] = i;
        }
    }

    return count;
}

void Entity_AddModifier(int agentId, float speedMult, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;

    // Refresh-not-stack: a repeating source (aura/formation re-applying the
    // same multiplier every tick) refreshes its slot instead of filling the
    // array with copies that MULTIPLY in Entity_GetSpeedMult (0.4 re-applied
    // twice used to become 0.16).
    for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
        if (a->modifiers[m].duration > 0.0f && a->modifiers[m].speedMult == speedMult) {
            if (duration > a->modifiers[m].duration) a->modifiers[m].duration = duration;
            return;
        }
    }
    for (int m = 0; m < MAX_AGENT_MODIFIERS; m++) {
        if (a->modifiers[m].duration <= 0.0f) {
            a->modifiers[m].speedMult = speedMult;
            a->modifiers[m].duration = duration;
            return;
        }
    }
    // no empty slot found — silently drop (minimal version, no eviction policy)
}

void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage,
                           float knockbackStrength, AgentTeam attackerTeam) {
    int ids[MAX_AGENTS];
    int count = Entity_GetNearbyTargets(center, radius, ids, MAX_AGENTS);

    for (int i = 0; i < count; i++) {
        int id = ids[i];
        Agent *a = &agentPool[id];
        if (a->team == attackerTeam) continue; // never hit your own team

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

void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult,
                         float duration, AgentTeam allyTeam) {
    int ids[MAX_AGENTS];
    int count = Entity_GetNearbyTargetsTeam(center, radius, allyTeam, ids, MAX_AGENTS);

    for (int i = 0; i < count; i++) {
        Entity_AddModifier(ids[i], speedMult, duration);
    }
}

int Entity_SpawnAgent(Vector3 position, float maxHealth, int element,
                      AgentTeam team, AgentArchetype archetype) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agentPool[i].active) {
            Agent *a = &agentPool[i];
            // Pool slots are reused — wipe the previous occupant's skill
            // cooldowns or the new agent inherits them (see skill_manager.h).
            SkillManager_ResetAgentCooldowns(i);
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
            a->team = team;
            a->archetype = archetype;
            a->isMeditating = false;
            a->meditateTimer = 0.0f;
            a->dashVelocity = (Vector3){ 0 };
            a->dashTimer = 0.0f;
            a->taijiActive = false;
            for (int s = 0; s < AGENT_SKILL_SLOTS; s++) {
                a->equippedSkills[s] = -1;
                a->equippedElements[s] = -1;
            }
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
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    // Actual movement breaks Thiền Định (external movement systems push
    // position every frame even when idle — only a real change cancels).
    if (a->isMeditating) {
        float dx = position.x - a->position.x;
        float dy = position.y - a->position.y;
        float dz = position.z - a->position.z;
        if (dx * dx + dy * dy + dz * dz > 1e-6f) CancelMeditate(a);
    }
    a->position = position;
}

const Agent *Entity_GetAgent(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return NULL;
    if (!agentPool[agentId].active) return NULL;
    return &agentPool[agentId];
}
