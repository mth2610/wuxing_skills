#include "{{name}}_skill.h"
#include "core/skill_helper.h"
#include "core/vfx_light.h"
#include "core/particle_radial_burst.h"
#include "core/utils_math.h"
#include "entities/entities.h"
#include "raymath.h"

#define MAX_INSTANCES 4
#define CASTING_DURATION 0.15f
#define DISSOLVE_DURATION 0.3f
#define VISUAL_TICK_INTERVAL 0.1f

typedef enum {
    STATE_CASTING,
    STATE_ATTACHED,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 casterPos;
    float stateTimer;
    float buffDuration;
    float buffRadius;
    int ownerAgentId;
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void Init{{Name}}Skill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void Cast{{Name}}Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!s_instances[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    float duration = 5.0f * params.sizeScale;
    float radius = 40.0f * params.sizeScale;

    s_instances[slot] = (SkillInstance){
        .state = STATE_CASTING,
        .casterPos = startPos,
        .stateTimer = 0.0f,
        .buffDuration = duration,
        .buffRadius = radius,
        .ownerAgentId = agentId,
        .active = true
    };

    Entity_ApplyAoEBuff(startPos, radius, 1.5f, duration);
    SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.5f);
}

void Update{{Name}}Skill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= CASTING_DURATION) {
                    s->state = STATE_ATTACHED;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_ATTACHED:
                if (fmodf(s->stateTimer, VISUAL_TICK_INTERVAL) < dt) {
                    VFXLight_Spawn(s->casterPos, ELEMENT_COLOR_WOOD, 60.0f, VISUAL_TICK_INTERVAL * 1.5f);
                }
                if (s->stateTimer >= s->buffDuration) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE:
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
        }
    }
}

void Draw{{Name}}Skill(void) {
    // VFXLight/particle draws are handled by their owning systems.
}

void Unload{{Name}}Skill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

bool Is{{Name}}SkillCoiling(void) {
    return false;
}

int Get{{Name}}SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    (void)outProjectiles; (void)maxProjectiles;
    return 0;
}

void Deactivate{{Name}}Projectile(int index) {
    (void)index;
}
