// boss/boss_system.c — ENGINE half: supports up to BOSS_MAX_INSTANCES (2)
// simultaneous bosses with optional waypoint marching routes (PvP Escort).
#include "boss/boss_system.h"
#include "skills/skill_manager.h"
#include "maps/map_manager.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static const float BOSS_TARGET_RANGE   = 25.0f;
static const float TAIJI_HP_FRACTION   = 0.30f;
static const float BOSS_PREFERRED_MIN  = 4.0f;
static const float BOSS_PREFERRED_MAX  = 6.5f;
static const float BOSS_DRIFT_MPS      = 1.4f;
static const float BOSS_STRAFE_MPS     = 0.8f;
static const float BOSS_ESCORT_RADIUS  = 6.0f;

typedef struct {
    const BossDef *def;
    int            agentId;
    int            phase;
    float          phaseTime;
    int            phaseSkillIdx[BOSS_MAX_PHASES];
    BossRoute      route;
    int            waypointIdx;
    bool           hasRoute;
    bool           reachedConvergence;
} BossSlot;

static BossSlot s_slots[BOSS_MAX_INSTANCES];

static void UpdateBossSlot(BossSlot *b, float dt) {
    if (b->def == NULL || b->agentId < 0) return;
    const Agent *self = Entity_GetAgent(b->agentId);
    if (self == NULL) {
        b->def = NULL;
        b->agentId = -1;
        return;
    }

    b->phaseTime += dt;

    float hpFrac = (self->maxHealth > 0.0f) ? self->health / self->maxHealth : 0.0f;
    int phase = 0;
    for (int i = 1; i < BOSS_MAX_PHASES; i++) {
        if (b->def->phaseHpThresholds[i] > 0.0f && hpFrac <= b->def->phaseHpThresholds[i]) {
            phase = i;
        }
    }
    if (phase != b->phase) {
        b->phase = phase;
        b->phaseTime = 0.0f;
        Entity_SetElement(b->agentId, (int)b->def->phaseElements[phase]);
    }

    if (hpFrac < TAIJI_HP_FRACTION && !self->taijiActive) {
        Entity_SetTaijiActive(b->agentId, true);
    }

    AgentTeam targetTeam = (self->team == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
    int ids[16];
    int n = Entity_GetNearbyTargetsTeam(self->position, BOSS_TARGET_RANGE, targetTeam, ids, 16);

    int target = -1;
    float bestSq = 0.0f;
    for (int i = 0; i < n; i++) {
        const Agent *cand = Entity_GetAgent(ids[i]);
        if (!cand) continue;
        float dx = cand->position.x - self->position.x;
        float dz = cand->position.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (target == -1 || dSq < bestSq) { target = ids[i]; bestSq = dSq; }
    }

    // Movement: Waypoint route marching (if assigned & not yet at convergence),
    // otherwise standard combat drift/strafe around target.
    if (self->vState == AGENT_GROUNDED && !Entity_IsCrowdControlled(b->agentId)) {
        if (b->hasRoute && !b->reachedConvergence && b->waypointIdx < b->route.waypointCount) {
            // Check escort link: ally hero within 6m & no enemy hero within 6m -> +25% speed
            bool allyNear = false;
            bool enemyNear = false;
            int nearAll[16];
            int nc = Entity_GetNearbyTargets(self->position, BOSS_ESCORT_RADIUS, nearAll, 16);
            for (int i = 0; i < nc; i++) {
                const Agent *na = Entity_GetAgent(nearAll[i]);
                if (!na || na->archetype != ARCH_HERO) continue;
                if (na->team == self->team) allyNear = true;
                else enemyNear = true;
            }
            float baseSpeed = (b->route.marchSpeedMps > 0.0f) ? b->route.marchSpeedMps : 1.2f;
            float speed = baseSpeed * (allyNear && !enemyNear ? 1.25f : 1.0f);
            // Slow down slightly if actively fighting close enemy
            if (enemyNear) speed *= 0.45f;

            Vector3 wp = b->route.waypoints[b->waypointIdx];
            float dx = wp.x - self->position.x;
            float dz = wp.z - self->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist <= 0.65f) {
                b->waypointIdx++;
                if (b->waypointIdx >= b->route.waypointCount) {
                    b->reachedConvergence = true;
                }
            } else {
                Vector3 pos = self->position;
                float step = speed * dt;
                if (step > dist) step = dist;
                pos.x += (dx / dist) * step;
                pos.z += (dz / dist) * step;
                pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                Entity_SetPosition(b->agentId, pos);
                self = Entity_GetAgent(b->agentId);
            }
        } else if (target >= 0) {
            const Agent *victim = Entity_GetAgent(target);
            if (victim) {
                float dx = victim->position.x - self->position.x;
                float dz = victim->position.z - self->position.z;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist > 0.001f) {
                    float nx = dx / dist, nz = dz / dist;
                    Vector3 pos = self->position;
                    if (dist > BOSS_PREFERRED_MAX) {
                        pos.x += nx * BOSS_DRIFT_MPS * dt;
                        pos.z += nz * BOSS_DRIFT_MPS * dt;
                    } else if (dist < BOSS_PREFERRED_MIN) {
                        pos.x -= nx * BOSS_DRIFT_MPS * dt;
                        pos.z -= nz * BOSS_DRIFT_MPS * dt;
                    } else {
                        float dir = (b->phase % 2 == 0) ? 1.0f : -1.0f;
                        pos.x += -nz * dir * BOSS_STRAFE_MPS * dt;
                        pos.z +=  nx * dir * BOSS_STRAFE_MPS * dt;
                    }
                    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                    Entity_SetPosition(b->agentId, pos);
                    self = Entity_GetAgent(b->agentId);
                }
            }
        }
    }

    int skillIdx = b->phaseSkillIdx[b->phase];
    if (skillIdx >= 0 && target >= 0 && SkillManager_CanCast(skillIdx, b->agentId)) {
        const Agent *victim = Entity_GetAgent(target);
        if (victim && CastSkill(skillIdx, b->agentId, self->position,
                                victim->position, (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
            SkillManager_TriggerCooldown(skillIdx, b->agentId,
                                         (b->def->castIntervalSeconds > 0.0f)
                                             ? b->def->castIntervalSeconds : 3.0f);
        }
    }
}

int Boss_SpawnSlot(int slot, const BossDef *def, Vector3 pos, AgentTeam team) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return -1;
    if (def == NULL || def->maxHealth <= 0.0f) return -1;

    // Kill previous occupant of this slot if still active
    if (s_slots[slot].agentId >= 0 && Entity_GetAgent(s_slots[slot].agentId) != NULL) {
        Entity_ApplyDamage(s_slots[slot].agentId, 1e9f, (Vector3){ 0 });
    }

    int id = Entity_SpawnAgent(pos, def->maxHealth, (int)def->phaseElements[0],
                               team, ARCH_BOSS);
    if (id < 0) return -1;

    memset(&s_slots[slot], 0, sizeof(s_slots[slot]));
    s_slots[slot].def = def;
    s_slots[slot].agentId = id;
    for (int i = 0; i < BOSS_MAX_PHASES; i++) {
        s_slots[slot].phaseSkillIdx[i] = def->phaseSkillNames[i]
            ? Skill_GetIndexByName(def->phaseSkillNames[i]) : -1;
    }
    return id;
}

void Boss_SetRoute(int slot, const BossRoute *route) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return;
    if (route == NULL || route->waypointCount <= 0) {
        s_slots[slot].hasRoute = false;
        s_slots[slot].reachedConvergence = false;
        return;
    }
    s_slots[slot].route = *route;
    if (s_slots[slot].route.waypointCount > BOSS_MAX_WAYPOINTS)
        s_slots[slot].route.waypointCount = BOSS_MAX_WAYPOINTS;
    s_slots[slot].waypointIdx = 0;
    s_slots[slot].hasRoute = true;
    s_slots[slot].reachedConvergence = false;
}

bool Boss_HasReachedConvergence(int slot) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return false;
    return s_slots[slot].reachedConvergence;
}

int Boss_GetSlotAgentId(int slot) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return -1;
    return (s_slots[slot].def != NULL) ? s_slots[slot].agentId : -1;
}

int Boss_GetSlotPhase(int slot) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return -1;
    return (s_slots[slot].def != NULL && s_slots[slot].agentId >= 0) ? s_slots[slot].phase : -1;
}

bool Boss_IsSlotAlive(int slot) {
    if (slot < 0 || slot >= BOSS_MAX_INSTANCES) return false;
    return s_slots[slot].def != NULL && Entity_GetAgent(s_slots[slot].agentId) != NULL;
}

void Boss_ClearAll(void) {
    for (int i = 0; i < BOSS_MAX_INSTANCES; i++) {
        if (s_slots[i].agentId >= 0 && Entity_GetAgent(s_slots[i].agentId) != NULL) {
            Entity_ApplyDamage(s_slots[i].agentId, 1e9f, (Vector3){ 0 });
        }
        memset(&s_slots[i], 0, sizeof(s_slots[i]));
        s_slots[i].agentId = -1;
    }
}

int Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team) {
    return Boss_SpawnSlot(0, def, pos, team);
}

void Boss_Update(float dt) {
    for (int i = 0; i < BOSS_MAX_INSTANCES; i++) {
        UpdateBossSlot(&s_slots[i], dt);
    }
}

void Boss_Draw(void) {
    for (int i = 0; i < BOSS_MAX_INSTANCES; i++) {
        if (s_slots[i].def == NULL || s_slots[i].def->drawVisual == NULL) continue;
        const Agent *self = Entity_GetAgent(s_slots[i].agentId);
        if (self == NULL) continue;
        s_slots[i].def->drawVisual(self, s_slots[i].phaseTime);
    }
}

int Boss_GetPhase(void)   { return Boss_GetSlotPhase(0); }
int Boss_GetAgentId(void) { return Boss_GetSlotAgentId(0); }
bool Boss_IsAlive(void)   { return Boss_IsSlotAlive(0); }
