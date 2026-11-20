// entities/entities.c
#include "entities.h"
#include "skills/skill_manager.h"
#include "maps/map_manager.h"
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

// Arena ring-out bounds — default matches MAP_API.md §3 (DEFAULT_ARENA);
// mutable per map via Entity_SetArenaBounds (the game screen sets them when
// pinning its match map). Real-world-scaled: 1 unit = 1 meter.
static Vector3 ARENA_CENTER = { 6.0f, 0.0f, 4.4f };
static float   ARENA_RADIUS = 18.0f;
static bool    s_usePlayableRect = false;
static float   s_rectMinX = 0.0f, s_rectMaxX = 100.0f;
static float   s_rectMinZ = 0.0f, s_rectMaxZ = 100.0f;

// 2D XZ Spatial Hash Grid (zero-alloc flat buckets, O(1) nearby queries)
#define SPATIAL_HASH_BUCKETS 1024
#define SPATIAL_CELL_SIZE    2.5f
static int          s_gridHead[SPATIAL_HASH_BUCKETS];
static int          s_gridNext[MAX_AGENTS];
static bool         s_gridDirty = true;
static unsigned int s_visitedStamp[MAX_AGENTS];
static unsigned int s_queryStamp = 1;

static inline unsigned int SpatialHashBucket(int cx, int cz) {
    unsigned int h = ((unsigned int)cx * 73856093u) ^ ((unsigned int)cz * 19349663u);
    return h & (SPATIAL_HASH_BUCKETS - 1u);
}

void Entity_RebuildSpatialGrid(void) {
    for (int b = 0; b < SPATIAL_HASH_BUCKETS; b++) s_gridHead[b] = -1;
    const float invCell = 1.0f / SPATIAL_CELL_SIZE;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agentPool[i].active) {
            s_gridNext[i] = -1;
            continue;
        }
        int cx = (int)floorf(agentPool[i].position.x * invCell);
        int cz = (int)floorf(agentPool[i].position.z * invCell);
        unsigned int b = SpatialHashBucket(cx, cz);
        s_gridNext[i] = s_gridHead[b];
        s_gridHead[b] = i;
    }
    s_gridDirty = false;
}
static const float   GRAVITY = 5.0f; // below real 9.81 m/s² by design (floatier game feel)
static const float   RING_OUT_KILL_Y = -2.0f;
static const float   DEFAULT_MAX_MANA = 100.0f;
static const float   MANA_REGEN_PER_SEC = 5.0f;
static const float   DEFAULT_MAX_STAMINA = 100.0f;
static const float   STAMINA_RUN_DRAIN_SEC = 10.0f;
static const float   STAMINA_REGEN_SEC = 18.0f;
static const float   STAMINA_RECOVER_THRESH = 15.0f;
// Thiền Định: 3s immobile channel; regen sized so an empty default pool
// (100) refills within the 3s window (34 * 3 > 100).
static const float   MEDITATE_DURATION = 3.0f;
static const float   MEDITATE_REGEN_PER_SEC = 34.0f;
static const float   MEDITATE_HP_PER_SEC = 28.0f;
// Dash: short horizontal burst, then cooldown gate.
static const float   DASH_DURATION = 0.15f;
static const float   DASH_COOLDOWN = 1.0f;
// Basic attack tuning (see Entity_ExecuteBasicAttack). Real-world-scaled —
// see root CLAUDE.md scale rules; melee range/wall-check radius sized to
// "arm's reach" and "standing next to your own wall" respectively.
static const float   BASIC_ATTACK_RANGE = 2.8f;
static const float   WALL_CHECK_RADIUS = 3.0f;
// Auto-target search radius — how far Entity_ExecuteBasicAttack looks for
// the nearest other agent, no explicit target needed.
static const float   AUTO_TARGET_RADIUS = 10.0f;
static float         s_preSparHealth[MAX_AGENTS] = { 0 };

void Entity_Init(void) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        agentPool[i] = (Agent){ 0 };
        s_visitedStamp[i] = 0;
    }
    s_usePlayableRect = false;
    s_gridDirty = true;
    Entity_RebuildSpatialGrid();
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

        if (a->hitFlashTimer > 0.0f) {
            a->hitFlashTimer -= dt;
            if (a->hitFlashTimer < 0.0f) a->hitFlashTimer = 0.0f;
        }
        if (a->dodgeGraceTimer > 0.0f) {
            a->dodgeGraceTimer -= dt;
            if (a->dodgeGraceTimer < 0.0f) a->dodgeGraceTimer = 0.0f;
        }

        // Thái Cực "Vô Sát" downside: the state lives on mana — a dry pool
        // exits it (Lôi's huge cost is the intended drain). Checked BEFORE
        // regen so the tick that drained to zero actually exits.
        if (a->taijiActive && a->mana <= 0.01f) {
            a->taijiActive = false;
        }

        if (a->mana < a->maxMana) {
            float regen = a->isMeditating ? MEDITATE_REGEN_PER_SEC : MANA_REGEN_PER_SEC;
            if (a->pveBonusesActive && a->pveBonuses.manaRegenMult > 0.0f) {
                regen *= a->pveBonuses.manaRegenMult;
            }
            a->mana += regen * dt;
            if (a->mana > a->maxMana) a->mana = a->maxMana;
        }

        if (a->isRunning) {
            a->stamina -= STAMINA_RUN_DRAIN_SEC * dt;
            if (a->stamina <= 0.0f) {
                a->stamina = 0.0f;
                a->staminaExhausted = true;
            }
        } else if (a->stamina < a->maxStamina) {
            float sRegen = a->isMeditating ? (STAMINA_REGEN_SEC * 1.8f) : STAMINA_REGEN_SEC;
            a->stamina += sRegen * dt;
            if (a->stamina > a->maxStamina) a->stamina = a->maxStamina;
            if (a->staminaExhausted && a->stamina >= STAMINA_RECOVER_THRESH) {
                a->staminaExhausted = false;
            }
        } else {
            a->staminaExhausted = false;
        }
        a->isRunning = false;

        if (a->isMeditating) {
            if (a->health > 0.0f && a->health < a->maxHealth) {
                a->health += MEDITATE_HP_PER_SEC * dt;
                if (a->health > a->maxHealth) a->health = a->maxHealth;
            }
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
            s_gridDirty = true;
            if (a->vState == AGENT_GROUNDED) {
                float gY = MapManager_GetGroundHeightAt(a->position.x, a->position.z);
                MapManager_ResolveCollision(&a->position, 0.35f, 0.45f, &gY);
                a->position.y = gY;
            }
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
            if (a->vState == AGENT_GROUNDED) {
                float gY = MapManager_GetGroundHeightAt(a->position.x, a->position.z);
                MapManager_ResolveCollision(&a->position, 0.35f, 0.45f, &gY);
                a->position.y = gY;
            }
            a->pullTimer -= dt;
            if (a->pullTimer < 0.0f) a->pullTimer = 0.0f;
            s_gridDirty = true;
        }

        Entity_CheckRingOut(i);

        if (a->vState == AGENT_RING_OUT_FALLING) {
            a->velocity.y -= GRAVITY * dt;
            a->position.y += a->velocity.y * dt;
            if (a->position.y < RING_OUT_KILL_Y) {
                a->active = false;
                s_gridDirty = true;
            }
        } else if (a->vState == AGENT_JUMPING) {
            a->velocity.y -= GRAVITY * dt;
            a->position.x += a->velocity.x * dt;
            a->position.y += a->velocity.y * dt;
            a->position.z += a->velocity.z * dt;
            s_gridDirty = true;
            float groundY = MapManager_GetGroundHeightAt(a->position.x, a->position.z);
            if (a->archetype != ARCH_MINION) {
                MapManager_ResolveCollision(&a->position, 0.35f, 0.45f, &groundY);
            }
            if (a->position.y <= groundY) {
                a->position.y = groundY;
                a->velocity = (Vector3){ 0 };
                a->vState = AGENT_GROUNDED;
                a->airJumpsUsed = 0;
            }
        }
    }

    // End Spar (Tỉ Thí) cleanly when any sparring hero reaches 1 HP:
    // return ALL sparring heroes to PK_MODE_PEACE and restore their pre-Spar HP.
    bool sparEnded = false;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agentPool[i].active && agentPool[i].archetype == ARCH_HERO &&
            agentPool[i].pkMode == PK_MODE_SPAR &&
            (agentPool[i].sparDefeated || agentPool[i].health <= 1.01f)) {
            sparEnded = true;
            break;
        }
    }
    if (sparEnded) {
        for (int j = 0; j < MAX_AGENTS; j++) {
            Agent *ha = &agentPool[j];
            if (!ha->active || ha->archetype != ARCH_HERO || ha->pkMode != PK_MODE_SPAR) continue;
            ha->pkMode = PK_MODE_PEACE;
            ha->duelFlag = false;
            ha->sparDefeated = false;
            float saved = (s_preSparHealth[j] > 1.0f) ? s_preSparHealth[j] : ha->maxHealth;
            if (saved > ha->maxHealth) saved = ha->maxHealth;
            ha->health = saved;
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
    s_gridDirty = true;
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

    if (a->pveBonusesActive && a->pveBonuses.damageTakenMult > 0.0f) {
        damage *= a->pveBonuses.damageTakenMult;
    }

    CancelMeditate(a);
    a->health -= damage;
    if (damage > 0.0f) a->hitFlashTimer = 0.12f;
    if (!a->isStatic) {
        a->velocity.x += knockback.x;
        a->velocity.y += knockback.y;
        a->velocity.z += knockback.z;

        // A real shove must enter the airborne state — the horizontal velocity
        // only integrates while AGENT_JUMPING (grounded knockback would be a
        // silent no-op). The pop height scales with the shove's strength so a
        // hard hit (Fire ~2.5 m/s) arcs long enough to be VISIBLY knocked back,
        // while a light nudge barely leaves the ground. Capped so nothing
        // launches to the moon.
        float kbSq = knockback.x * knockback.x + knockback.z * knockback.z;
        if (kbSq > 0.25f && a->vState == AGENT_GROUNDED) {
            float pop = sqrtf(kbSq);           // arc height ∝ horizontal impulse
            if (pop < 1.2f) pop = 1.2f;
            if (pop > 4.0f) pop = 4.0f;
            if (a->velocity.y < pop) a->velocity.y = pop;
            a->vState = AGENT_JUMPING;
        }
    }

    if (a->health <= 0.0f) {
        a->health = 0.0f;
        a->active = false;
        s_gridDirty = true;
    }
}

bool Entity_CanDamageTarget(int attackerId, int targetId) {
    if (attackerId == targetId) return false;
    const Agent *att = Entity_GetAgent(attackerId);
    const Agent *tgt = Entity_GetAgent(targetId);
    if (!att || !tgt) return false;

    if (att->archetype == ARCH_HERO && tgt->archetype == ARCH_HERO) {
        // Tỉ Thí (non-lethal, stops at 1 HP) is allowed anywhere, including Safe Zone.
        if (att->pkMode == PK_MODE_SPAR && tgt->pkMode == PK_MODE_SPAR) return true;
        if (att->inSafeZone || tgt->inSafeZone) return false;
        if (att->team != tgt->team) return true;
        if (att->pkMode == PK_MODE_SLAUGHTER || tgt->pkMode == PK_MODE_SLAUGHTER) return true;
        return att->duelFlag && tgt->duelFlag;
    }
    return att->team != tgt->team;
}

void Entity_ApplyDamageFrom(int targetId, int attackerId, float baseDamage,
                            float skillScale, Vector3 knockback) {
    if (targetId < 0 || targetId >= MAX_AGENTS) return;
    Agent *tgt = &agentPool[targetId];
    if (!tgt->active) return;

    const Agent *att = (attackerId >= 0) ? Entity_GetAgent(attackerId) : NULL;
    if (att != NULL && !Entity_CanDamageTarget(attackerId, targetId)) return;

    float dmg = baseDamage;
    bool isHeroVsHero = (tgt->archetype == ARCH_HERO) && (att == NULL || att->archetype == ARCH_HERO);
    if (!isHeroVsHero && att != NULL && dmg > 0.0f) {
        float lvlMult = (skillScale > 0.0f) ? skillScale :
                        ((att->lastCastDamageScale > 0.0f) ? att->lastCastDamageScale : 1.0f);
        dmg *= lvlMult * Entity_GetDamageOutMult(attackerId);
    }

    if (!isHeroVsHero && tgt->pveBonusesActive && tgt->pveBonuses.damageTakenMult > 0.0f && dmg > 0.0f) {
        dmg *= tgt->pveBonuses.damageTakenMult;
    }

    CancelMeditate(tgt);
    if (dmg > 0.0f) tgt->hitFlashTimer = 0.12f;
    if (att != NULL && dmg > 0.0f) {
        tgt->lastAttackerId = attackerId;
    }
    bool isNonLethalSpar = (att != NULL && att->archetype == ARCH_HERO && tgt->archetype == ARCH_HERO &&
                            att->team == tgt->team &&
                            att->pkMode == PK_MODE_SPAR && tgt->pkMode == PK_MODE_SPAR);
    if (isNonLethalSpar && dmg > 0.0f && (tgt->health - dmg) <= 1.0f) {
        tgt->health = 1.0f;
        tgt->sparDefeated = true;
    } else {
        tgt->health -= dmg;
    }
    if (tgt->health > tgt->maxHealth) tgt->health = tgt->maxHealth;
    if (!tgt->isStatic) {
        tgt->velocity.x += knockback.x;
        tgt->velocity.y += knockback.y;
        tgt->velocity.z += knockback.z;

        float kbSq = knockback.x * knockback.x + knockback.z * knockback.z;
        if (kbSq > 0.25f && tgt->vState == AGENT_GROUNDED) {
            float pop = sqrtf(kbSq);
            if (pop < 1.2f) pop = 1.2f;
            if (pop > 4.0f) pop = 4.0f;
            if (tgt->velocity.y < pop) tgt->velocity.y = pop;
            tgt->vState = AGENT_JUMPING;
        }
    }

    if (tgt->health <= 0.0f) {
        tgt->health = 0.0f;
        tgt->active = false;
        s_gridDirty = true;
    }
}

void Entity_SetDuelFlag(int agentId, bool on) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    agentPool[agentId].duelFlag = on;
    agentPool[agentId].pkMode = on ? PK_MODE_SPAR : PK_MODE_PEACE;
}

bool Entity_GetDuelFlag(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a ? (a->duelFlag || a->pkMode != PK_MODE_PEACE) : false;
}

void Entity_SetPkMode(int agentId, AgentPkMode mode) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    if (agentPool[agentId].pkMode != PK_MODE_SPAR && mode == PK_MODE_SPAR) {
        s_preSparHealth[agentId] = agentPool[agentId].health;
    }
    agentPool[agentId].pkMode = mode;
    agentPool[agentId].duelFlag = (mode != PK_MODE_PEACE);
}

AgentPkMode Entity_GetPkMode(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a ? a->pkMode : PK_MODE_PEACE;
}

void Entity_AddInfamy(int agentId, int delta) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    int next = agentPool[agentId].infamyLevel + delta;
    if (next < 0) next = 0;
    agentPool[agentId].infamyLevel = next;
}

int Entity_GetInfamyLevel(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a ? a->infamyLevel : 0;
}

bool Entity_ConsumeSparDefeated(int agentId, int *outAttackerId) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return false;
    if (!agentPool[agentId].sparDefeated) return false;
    agentPool[agentId].sparDefeated = false;
    if (outAttackerId) *outAttackerId = agentPool[agentId].lastAttackerId;
    return true;
}

void Entity_SetSafeZone(int agentId, bool inSafe) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    agentPool[agentId].inSafeZone = inSafe;
    if (inSafe) {
        agentPool[agentId].duelFlag = false;
        agentPool[agentId].pkMode = PK_MODE_PEACE;
        agentPool[agentId].infamyLevel = 0;
        agentPool[agentId].sparDefeated = false;
    }
}

bool Entity_IsInSafeZone(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a ? a->inSafeZone : false;
}

void Entity_SetStatic(int agentId, bool isStatic) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    agentPool[agentId].isStatic = isStatic;
}

void Entity_SetLastCast(int agentId, int skillIndex, float pveDamageScale) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    agentPool[agentId].lastCastSkillIndex = skillIndex;
    agentPool[agentId].lastCastDamageScale = (pveDamageScale > 0.0f) ? pveDamageScale : 1.0f;
}

void Entity_Jump(int agentId, float force) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (a->vState == AGENT_GROUNDED) {
        CancelMeditate(a);
        a->velocity.y = force;
        a->vState = AGENT_JUMPING;
        a->airJumpsUsed = 1;
    }
}

bool Entity_PerformJump(int agentId, float verticalForce, Vector3 horizontalVelocity) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    Agent *a = &agentPool[agentId];
    if (!a->active || a->stunTimer > 0.0f || a->pullTimer > 0.0f) return false;
    if (a->vState == AGENT_RING_OUT_FALLING) return false;

    if (a->vState == AGENT_GROUNDED) {
        a->airJumpsUsed = 1;
    } else if (a->vState == AGENT_JUMPING && a->airJumpsUsed < 2) {
        a->airJumpsUsed++;
    } else {
        return false;
    }
    CancelMeditate(a);
    a->velocity = (Vector3){ horizontalVelocity.x, verticalForce, horizontalVelocity.z };
    a->vState = AGENT_JUMPING;
    if (a->dodgeGraceTimer < 0.45f) a->dodgeGraceTimer = 0.45f;
    return true;
}

void Entity_SetRunning(int agentId, bool running) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !agentPool[agentId].active) return;
    agentPool[agentId].isRunning = running;
}

bool Entity_IsRunning(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    return a ? (a->isRunning && !a->staminaExhausted) : false;
}

void Entity_ArmDodgeGrace(int agentId, float duration) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (duration > a->dodgeGraceTimer) a->dodgeGraceTimer = duration;
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
    if (a->dodgeGraceTimer < 0.55f) a->dodgeGraceTimer = 0.55f;
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

float Entity_GetMeditateProgress(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (a == NULL || !a->isMeditating || MEDITATE_DURATION <= 0.0f) return 0.0f;
    float p = 1.0f - (a->meditateTimer / MEDITATE_DURATION);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
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
    // + 2 Dương (Hỏa 2 / Kim 4) (+ optional 5th Earth/Thổ slot) enters the state;
    // any other loadout leaves it (boss/'s forced flag is reapplied by Boss_Update).
    int yin  = counts[0] + counts[1];
    int yang = counts[2] + counts[4];
    a->taijiActive = (filled >= 4 && yin == 2 && yang == 2);

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

// --- Net snapshot mirroring (see header — net/ transport only) ---
static bool s_netMark[MAX_AGENTS];

void Entity_NetSyncBegin(void) {
    for (int i = 0; i < MAX_AGENTS; i++) s_netMark[i] = false;
}

void Entity_NetSyncAgent(int agentId, Vector3 pos, float health, float maxHealth,
                         float mana, float maxMana, int element,
                         AgentTeam team, AgentArchetype archetype,
                         bool taiji, bool meditating, bool stealthed, bool duelFlag) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) {
        // Fresh mirror slot — zero the local-only state once.
        *a = (Agent){ 0 };
        a->vState = AGENT_GROUNDED;
        for (int s = 0; s < AGENT_SKILL_SLOTS; s++) {
            a->equippedSkills[s] = -1;
            a->equippedElements[s] = -1;
        }
    }
    a->active = true;
    if (pos.x != a->position.x || pos.z != a->position.z) { /* keep smooth */ }
    if (health < a->health - 0.5f) {
        a->hitFlashTimer = 0.12f;
    }
    a->position = pos;
    a->health = health;
    a->maxHealth = maxHealth;
    a->mana = mana;
    a->maxMana = maxMana;
    a->currentElement = element;
    a->team = team;
    a->archetype = archetype;
    a->taijiActive = taiji;
    a->isMeditating = meditating;
    a->isStealthed = stealthed;
    a->duelFlag = duelFlag;
    s_netMark[agentId] = true;
}

void Entity_NetSyncEnd(void) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agentPool[i].active && !s_netMark[i]) agentPool[i].active = false;
    }
    s_gridDirty = true;
}

void Entity_SetFacingYaw(int agentId, float yawRadians) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    if (!agentPool[agentId].active) return;
    agentPool[agentId].facingYaw = yawRadians;
}

void Entity_SetNetMonsterMeta(int agentId, unsigned char packedMeta) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    if (!agentPool[agentId].active) return;
    agentPool[agentId].netMonsterMeta = packedMeta;
}

void Entity_TickClientVisuals(float dt) {
    if (dt <= 0.0f) return;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agentPool[i].active) continue;
        if (agentPool[i].hitFlashTimer > 0.0f) {
            agentPool[i].hitFlashTimer -= dt;
            if (agentPool[i].hitFlashTimer < 0.0f) agentPool[i].hitFlashTimer = 0.0f;
        }
    }
}

void Entity_SetElement(int agentId, int element) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    if (element < 0 || element > 4) return;
    a->currentElement = element;
}

void Entity_SetAgentTeam(int agentId, AgentTeam team) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->team = team;
}

void Entity_ScaleMaxHealth(int agentId, float mult) {
    if (agentId < 0 || agentId >= MAX_AGENTS || mult <= 0.0f) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->maxHealth *= mult;
    a->health *= mult;
    if (a->maxHealth < 1.0f) a->maxHealth = 1.0f;
    if (a->health < 1.0f) a->health = 1.0f;
}

void Entity_SetPvEBonuses(int agentId, const AgentPvEBonuses *bonuses) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;

    float baseHp = (a->baseMaxHealth > 0.0f) ? a->baseMaxHealth : a->maxHealth;
    float baseMp = (a->baseMaxMana > 0.0f) ? a->baseMaxMana : a->maxMana;

    if (bonuses == NULL) {
        a->pveBonusesActive = false;
        a->pveBonuses = (AgentPvEBonuses){ 0 };
        a->maxHealth = baseHp;
        a->maxMana = baseMp;
        if (a->health > a->maxHealth) a->health = a->maxHealth;
        if (a->mana > a->maxMana) a->mana = a->maxMana;
        return;
    }

    AgentPvEBonuses norm = *bonuses;
    if (norm.moveSpeedMult <= 0.0f) norm.moveSpeedMult = 1.0f;
    if (norm.damageOutMult <= 0.0f) norm.damageOutMult = 1.0f;
    if (norm.damageTakenMult <= 0.0f) norm.damageTakenMult = 1.0f;
    if (norm.manaRegenMult <= 0.0f) norm.manaRegenMult = 1.0f;

    float prevMaxHp = a->maxHealth;
    float prevMaxMp = a->maxMana;
    a->pveBonusesActive = true;
    a->pveBonuses = norm;
    a->maxHealth = fmaxf(1.0f, baseHp + norm.bonusMaxHealth);
    a->maxMana = fmaxf(1.0f, baseMp + norm.bonusMaxMana);
    if (prevMaxHp > 0.0f) {
        a->health = fminf(a->maxHealth, a->health * (a->maxHealth / prevMaxHp));
    } else {
        a->health = a->maxHealth;
    }
    if (prevMaxMp > 0.0f) {
        a->mana = fminf(a->maxMana, a->mana * (a->maxMana / prevMaxMp));
    } else {
        a->mana = a->maxMana;
    }
}

float Entity_GetDamageOutMult(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a || !a->pveBonusesActive || a->pveBonuses.damageOutMult <= 0.0f) return 1.0f;
    return a->pveBonuses.damageOutMult;
}

float Entity_GetDamageTakenMult(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a || !a->pveBonusesActive || a->pveBonuses.damageTakenMult <= 0.0f) return 1.0f;
    return a->pveBonuses.damageTakenMult;
}

float Entity_GetSpeedMult(int agentId) {
    const Agent *a = Entity_GetAgent(agentId);
    if (!a) return 1.0f;
    float mult = 1.0f;
    if (a->pveBonusesActive && a->pveBonuses.moveSpeedMult > 0.0f) {
        mult *= a->pveBonuses.moveSpeedMult;
    }
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

void Entity_RestoreMana(int agentId, float amount) {
    if (agentId < 0 || agentId >= MAX_AGENTS || amount <= 0.0f) return;
    Agent *a = &agentPool[agentId];
    if (!a->active) return;
    a->mana += amount;
    if (a->mana > a->maxMana) a->mana = a->maxMana;
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
        if (!Entity_CanDamageTarget(attackerId, nearbyIds[i])) continue;
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
                Entity_ApplyDamageFrom(targetId, attackerId, 5.0f, 1.0f, (Vector3){ 0 });
                break;
            case BASIC_ATTACK_KICK: {
                Vector3 kb = (GetRandomValue(0, 99) < 12) ? Vector3Scale(pushDir, 1.4f) : (Vector3){ 0 };
                Entity_ApplyDamageFrom(targetId, attackerId, 8.0f, 1.0f, kb);
                break;
            }
            case BASIC_ATTACK_PALM:
                Entity_ApplyDamageFrom(targetId, attackerId, 12.0f, 1.0f, (Vector3){ 0 });
                if (GetRandomValue(0, 99) < 15) {
                    Entity_ApplyLaunch(targetId, 2.2f, Vector3Scale(pushDir, 1.2f));
                }
                break;
        }
    }

    Vector3 wallPos;
    int wallElement;
    if (SkillManager_FindNearbyWall(attacker->position, WALL_CHECK_RADIUS, &wallPos, &wallElement)) {
        // Free elemental bonus hit — no mana cost, this whole path never
        // touches Entity_TrySpendMana. Applies regardless of melee range
        // (the rock/bolt is what closes the distance, not the fist).
        Entity_ApplyDamageFrom(targetId, attackerId, 6.0f, 1.0f, (Vector3){ 0 });
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

    bool outOfBounds = false;
    if (s_usePlayableRect) {
        if (a->position.x < s_rectMinX || a->position.x > s_rectMaxX ||
            a->position.z < s_rectMinZ || a->position.z > s_rectMaxZ) {
            outOfBounds = true;
        } else if (a->vState == AGENT_GROUNDED) {
            float gy = MapManager_GetGroundHeightAt(a->position.x, a->position.z);
            if (gy < -50.0f) outOfBounds = true;
        }
    } else {
        float dx = a->position.x - ARENA_CENTER.x;
        float dz = a->position.z - ARENA_CENTER.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > ARENA_RADIUS * ARENA_RADIUS) outOfBounds = true;
    }

    if (outOfBounds) {
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

void Entity_SetArenaBounds(Vector3 center, float radius) {
    if (radius <= 0.0f) return;
    ARENA_CENTER = center;
    ARENA_RADIUS = radius;
}

void Entity_GetArenaBounds(Vector3 *outCenter, float *outRadius) {
    if (outCenter != NULL) *outCenter = ARENA_CENTER;
    if (outRadius != NULL) *outRadius = ARENA_RADIUS;
}

void Entity_SetPlayableRect(float minX, float maxX, float minZ, float maxZ) {
    s_usePlayableRect = true;
    s_rectMinX = minX;
    s_rectMaxX = maxX;
    s_rectMinZ = minZ;
    s_rectMaxZ = maxZ;
}

void Entity_ClearPlayableRect(void) {
    s_usePlayableRect = false;
}

static int QueryNearbySpatial(Vector3 center, float radius, bool filterTeam, AgentTeam team,
                              int *outIds, int maxIds) {
    if (outIds == NULL || maxIds <= 0 || radius < 0.0f) return 0;
    float radiusSq = radius * radius;

    // Large map-wide queries fall back to flat scan (avoids enumerating huge cell rectangles).
    if (radius > 25.0f) {
        int count = 0;
        for (int i = 0; i < MAX_AGENTS && count < maxIds; i++) {
            const Agent *a = &agentPool[i];
            if (!a->active) continue;
            if (filterTeam && a->team != team) continue;
            float dx = a->position.x - center.x;
            float dz = a->position.z - center.z;
            if (dx * dx + dz * dz <= radiusSq) outIds[count++] = i;
        }
        return count;
    }

    if (s_gridDirty) Entity_RebuildSpatialGrid();

    const float invCell = 1.0f / SPATIAL_CELL_SIZE;
    int cx0 = (int)floorf((center.x - radius) * invCell);
    int cx1 = (int)floorf((center.x + radius) * invCell);
    int cz0 = (int)floorf((center.z - radius) * invCell);
    int cz1 = (int)floorf((center.z + radius) * invCell);

    unsigned int stamp = ++s_queryStamp;
    if (stamp == 0) {
        for (int i = 0; i < MAX_AGENTS; i++) s_visitedStamp[i] = 0;
        stamp = ++s_queryStamp;
    }

    int count = 0;
    for (int cz = cz0; cz <= cz1 && count < maxIds; cz++) {
        for (int cx = cx0; cx <= cx1 && count < maxIds; cx++) {
            unsigned int b = SpatialHashBucket(cx, cz);
            for (int id = s_gridHead[b]; id >= 0; id = s_gridNext[id]) {
                if (s_visitedStamp[id] == stamp) continue;
                s_visitedStamp[id] = stamp;
                const Agent *a = &agentPool[id];
                if (!a->active) continue;
                if (filterTeam && a->team != team) continue;
                float dx = a->position.x - center.x;
                float dz = a->position.z - center.z;
                if (dx * dx + dz * dz <= radiusSq) {
                    outIds[count++] = id;
                    if (count >= maxIds) break;
                }
            }
        }
    }
    return count;
}

int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds) {
    return QueryNearbySpatial(center, radius, false, TEAM_ALLY, outIds, maxIds);
}

int Entity_GetNearbyTargetsTeam(Vector3 center, float radius, AgentTeam filter,
                                int *outIds, int maxIds) {
    return QueryNearbySpatial(center, radius, true, filter, outIds, maxIds);
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

    // Check if an active PvP/Spar hero exists so same-team (TEAM_ALLY) Open-World
    // players can damage each other with AoE skills during Tỉ Thí or Đồ Sát.
    int sparHeroCount = 0;
    bool anySlaughter = false;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agentPool[i].active && agentPool[i].archetype == ARCH_HERO &&
            agentPool[i].team == attackerTeam) {
            if (agentPool[i].pkMode == PK_MODE_SPAR) sparHeroCount++;
            if (agentPool[i].pkMode == PK_MODE_SLAUGHTER && !agentPool[i].inSafeZone) anySlaughter = true;
        }
    }

    for (int i = 0; i < count; i++) {
        int id = ids[i];
        Agent *a = &agentPool[id];
        if (a->team == attackerTeam) {
            bool canSparAoE = (a->archetype == ARCH_HERO && sparHeroCount >= 2 && a->pkMode == PK_MODE_SPAR);
            bool canSlaughterAoE = (a->archetype == ARCH_HERO && anySlaughter && !a->inSafeZone);
            if (!canSparAoE && !canSlaughterAoE) continue;
        }

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

        if (a->archetype == ARCH_HERO && a->pkMode == PK_MODE_SPAR &&
            damage > 0.0f && (a->health - damage) <= 1.0f) {
            a->health = 1.0f;
            a->sparDefeated = true;
            a->hitFlashTimer = 0.12f;
        } else {
            Entity_ApplyDamage(id, damage, knockback);
        }
    }

    // End Spar (Tỉ Thí) cleanly when any sparring hero reaches 1 HP:
    // return ALL sparring heroes to PK_MODE_PEACE and restore their pre-Spar HP.
    bool sparEnded = false;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agentPool[i].active && agentPool[i].archetype == ARCH_HERO &&
            agentPool[i].pkMode == PK_MODE_SPAR &&
            (agentPool[i].sparDefeated || agentPool[i].health <= 1.01f)) {
            sparEnded = true;
            break;
        }
    }
    if (sparEnded) {
        for (int j = 0; j < MAX_AGENTS; j++) {
            Agent *ha = &agentPool[j];
            if (!ha->active || ha->archetype != ARCH_HERO || ha->pkMode != PK_MODE_SPAR) continue;
            ha->pkMode = PK_MODE_PEACE;
            ha->duelFlag = false;
            ha->sparDefeated = false;
            float saved = (s_preSparHealth[j] > 1.0f) ? s_preSparHealth[j] : ha->maxHealth;
            if (saved > ha->maxHealth) saved = ha->maxHealth;
            ha->health = saved;
        }
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
            a->maxStamina = DEFAULT_MAX_STAMINA;
            a->stamina = DEFAULT_MAX_STAMINA;
            a->staminaExhausted = false;
            a->isRunning = false;
            a->airJumpsUsed = 0;
            a->baseMaxHealth = maxHealth;
            a->baseMaxMana = DEFAULT_MAX_MANA;
            a->pveBonusesActive = false;
            a->pveBonuses = (AgentPvEBonuses){ 0 };
            a->duelFlag = false;
            a->inSafeZone = false;
            a->isStatic = false;
            a->lastCastSkillIndex = -1;
            a->lastCastDamageScale = 1.0f;
            a->hitFlashTimer = 0.0f;
            a->dodgeGraceTimer = 0.0f;
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
            s_gridDirty = true;
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
    s_gridDirty = true;
}

const Agent *Entity_GetAgent(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return NULL;
    if (!agentPool[agentId].active) return NULL;
    return &agentPool[agentId];
}
