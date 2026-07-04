#include "{{name}}_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/path_spline.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "raymath.h"

#define MAX_INSTANCES 16
#define INSTANCE_SPACING 35.0f
#define STAGGER_DURATION 0.5f
#define RISING_DURATION 0.25f
#define ACTIVE_DURATION 1.5f
#define DISSOLVE_DURATION 0.4f

typedef enum {
    STATE_CASTING,
    STATE_WAITING,
    STATE_RISING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    float stateTimer;
    float waitTime;
    float radius;
    float maxHeight;
    float currentHeight;
    int ownerAgentId;
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void Init{{Name}}Skill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void Cast{{Name}}Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    Vector3 rawPath[33];
    int rawCount = 0;
    if (params.pathPointCount > 1) {
        for (int i = 0; i < params.pathPointCount && i < 32; i++) rawPath[rawCount++] = params.pathPoints[i];
    } else {
        rawPath[rawCount++] = startPos;
        rawPath[rawCount++] = target;
    }

    Vector3 sampled[MAX_INSTANCES];
    int sampledCount = SamplePath(rawPath, rawCount, INSTANCE_SPACING, sampled, MAX_INSTANCES);
    if (sampledCount <= 0) return;

    for (int p = 0; p < sampledCount; p++) {
        int slot = -1;
        for (int i = 0; i < MAX_INSTANCES; i++) {
            if (!s_instances[i].active) { slot = i; break; }
        }
        if (slot < 0) break;

        s_instances[slot] = (SkillInstance){
            .state = STATE_CASTING,
            .position = sampled[p],
            .stateTimer = 0.0f,
            .waitTime = (float)p / (float)sampledCount * STAGGER_DURATION,
            .radius = 12.0f * params.sizeScale,
            .maxHeight = 70.0f * params.sizeScale,
            .currentHeight = 0.0f,
            .ownerAgentId = agentId,
            .active = true
        };
    }
    SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.6f);
}

void Update{{Name}}Skill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= 0.05f) {
                    s->state = STATE_WAITING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_WAITING:
                if (s->stateTimer >= s->waitTime) {
                    s->state = STATE_RISING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_RISING: {
                float t = SmoothStep01(s->stateTimer / RISING_DURATION);
                s->currentHeight = Math_Mix(0.0f, s->maxHeight, t);
                if (s->stateTimer >= RISING_DURATION) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    s->currentHeight = s->maxHeight;
                    ApplyAoEDamage(s->position, s->radius * 1.3f, 18.0f, 0.0f);
                }
                break;
            }

            case STATE_ACTIVE:
                if (s->stateTimer >= ACTIVE_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE: {
                float t = SmoothStep01(s->stateTimer / DISSOLVE_DURATION);
                s->currentHeight = Math_Mix(s->maxHeight, 0.0f, t);
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
            }
        }
    }
}

void Draw{{Name}}Skill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->currentHeight <= 0.01f) continue;
        Vector3 top = { s->position.x, s->position.y + s->currentHeight, s->position.z };
        DrawCoreCylinder(s->position, top, s->radius, s->radius * 0.5f, 10, ELEMENT_COLOR_WOOD);
    }
}

void Unload{{Name}}Skill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

SKILL_EMPTY_PROJECTILE_API({{Name}})
