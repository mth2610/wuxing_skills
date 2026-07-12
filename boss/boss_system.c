// boss/boss_system.c — ENGINE half: written once, never edited to add a
// boss (new boss = new <name>_def.c data file). Pure logic — no VFX headers.
#include "boss/boss_system.h"
#include "core/skill_manager.h" // CastSkill/CanCast/TriggerCooldown/GetIndexByName
#include <math.h>
#include <stddef.h>

static const float BOSS_TARGET_RANGE = 25.0f; // meters
static const float TAIJI_HP_FRACTION = 0.30f; // Module 6 trigger threshold

static const BossDef *s_def = NULL;
static int   s_agentId = -1;
static int   s_phase = 0;
static float s_phaseTime = 0.0f;
static int   s_phaseSkillIdx[BOSS_MAX_PHASES];

int Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team) {
    if (def == NULL || def->maxHealth <= 0.0f) return -1;

    int id = Entity_SpawnAgent(pos, def->maxHealth, (int)def->phaseElements[0],
                               team, ARCH_BOSS);
    if (id < 0) return -1;

    s_def = def;
    s_agentId = id;
    s_phase = 0;
    s_phaseTime = 0.0f;
    for (int i = 0; i < BOSS_MAX_PHASES; i++) {
        s_phaseSkillIdx[i] = def->phaseSkillNames[i]
            ? Skill_GetIndexByName(def->phaseSkillNames[i]) : -1;
    }
    return id;
}

void Boss_Update(float dt) {
    if (s_def == NULL || s_agentId < 0) return;
    const Agent *self = Entity_GetAgent(s_agentId);
    if (self == NULL) { // died (HP or ring-out) — boss fight over
        s_def = NULL;
        s_agentId = -1;
        return;
    }

    s_phaseTime += dt;

    // --- Phase from %HP (highest phase whose threshold is reached) ---
    float hpFrac = (self->maxHealth > 0.0f) ? self->health / self->maxHealth : 0.0f;
    int phase = 0;
    for (int i = 1; i < BOSS_MAX_PHASES; i++) {
        if (s_def->phaseHpThresholds[i] > 0.0f && hpFrac <= s_def->phaseHpThresholds[i]) {
            phase = i;
        }
    }
    if (phase != s_phase) {
        s_phase = phase;
        s_phaseTime = 0.0f;
        // Biến hệ — the visual cue (rune color) follows currentElement.
        Entity_SetElement(s_agentId, (int)s_def->phaseElements[phase]);
    }

    // --- Thái Cực below 30% HP (Module 6): boss enters the state — clash
    // immunity + monochrome world (main/game reads any-taiji-active). ---
    if (hpFrac < TAIJI_HP_FRACTION && !self->taijiActive) {
        Entity_SetTaijiActive(s_agentId, true);
    }

    // --- AI: nearest opposing-team agent in range, cast on cooldown ---
    int skillIdx = s_phaseSkillIdx[s_phase];
    if (skillIdx < 0) return;

    AgentTeam targetTeam = (self->team == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
    int ids[16];
    int n = Entity_GetNearbyTargetsTeam(self->position, BOSS_TARGET_RANGE, targetTeam, ids, 16);
    if (n <= 0) return;

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
    if (target < 0) return;

    if (SkillManager_CanCast(skillIdx, s_agentId)) {
        const Agent *victim = Entity_GetAgent(target);
        if (victim && CastSkill(skillIdx, s_agentId, self->position,
                                victim->position, (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
            SkillManager_TriggerCooldown(skillIdx, s_agentId,
                                         (s_def->castIntervalSeconds > 0.0f)
                                             ? s_def->castIntervalSeconds : 3.0f);
        }
    }
}

void Boss_Draw(void) {
    if (s_def == NULL || s_def->drawVisual == NULL) return;
    const Agent *self = Entity_GetAgent(s_agentId);
    if (self == NULL) return;
    s_def->drawVisual(self, s_phaseTime);
}

int Boss_GetPhase(void) {
    return (s_def != NULL && s_agentId >= 0) ? s_phase : -1;
}

int Boss_GetAgentId(void) {
    return (s_def != NULL) ? s_agentId : -1;
}

bool Boss_IsAlive(void) {
    return s_def != NULL && Entity_GetAgent(s_agentId) != NULL;
}
