#include "leaf_whirlwind_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/composition/visual_composer.h"
#include "entities/entities.h"
#include "raymath.h"

#define MAX_INSTANCES 16
#define CASTING_DURATION 0.3f
#define ACTIVE_DURATION 2.0f
#define DISSOLVE_DURATION 0.4f
#define ORBITAL_COUNT 6

typedef enum {
    STATE_CASTING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    float stateTimer;
    float radius;
    int ownerAgentId;
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "leaf_whirlwind_skill_params.inl"

void InitLeafWhirlwindSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;

#define LEAF_WHIRLWIND_TUNABLE_COUNT 3
    static SkillTunableEntry s_tunables[LEAF_WHIRLWIND_TUNABLE_COUNT];
    int tn = 0;
#include "leaf_whirlwind_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("LEAF_WHIRLWIND");
    SkillTunables_LoadPersisted("skills/wood/leaf_whirlwind_skill/leaf_whirlwind_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void CastLeafWhirlwindSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = target,
            .stateTimer = 0.0f,
            .radius = s_baseRadius * params.sizeScale,
            .ownerAgentId = agentId,
            .active = true
        };
        SpawnCastEffect(target, EFFECT_PRESET_WOOD_BLOOM, s_effectScale * params.sizeScale * 0.6f);
        return;
    }
}

void UpdateLeafWhirlwindSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= CASTING_DURATION) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    SpawnImpactEffect(s->position, EFFECT_PRESET_WOOD_BLOOM, s_effectScale);
                    VFX_SpawnOrbitals(s->position, EFFECT_PRESET_WOOD_BLOOM, ORBITAL_COUNT, s->radius, ACTIVE_DURATION);
                }
                break;

            case STATE_ACTIVE: {
                // Simple pull: while the single test enemy is inside radius,
                // keep refreshing Entity_ApplyPull toward the whirlwind's
                // center (entities/entities.h §12) — same per-tick refresh
                // cadence as stone_prison_skill's stun.
                float dx = enemyPos.x - s->position.x;
                float dz = enemyPos.z - s->position.z;
                float distSq = dx * dx + dz * dz;
                float checkRad = s->radius + enemyRadius;
                if (distSq <= checkRad * checkRad) {
                    int hitAgentId = SkillManager_GetEnemyAgentId();
                    if (hitAgentId >= 0) {
                        Entity_ApplyPull(hitAgentId, s->position, s_pullSpeed, 0.15f);
                    }
                }
                if (s->stateTimer >= ACTIVE_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;
            }

            case STATE_DISSOLVE:
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
        }
    }
}

void DrawLeafWhirlwindSkill(void) {
    // No-op — visuals are VFX_ComposeCast/Impact + VFX_SpawnOrbitals (see
    // Update), both self-managed pools ticked/drawn by SkillHelper_Update
    // and the composition layer. Nothing extra to draw here.
}

void UnloadLeafWhirlwindSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

SKILL_EMPTY_PROJECTILE_API(LeafWhirlwind)
