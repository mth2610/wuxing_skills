#include "{{name}}_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "raymath.h"

#define MAX_INSTANCES 16
#define RISING_DURATION 0.4f
#define ACTIVE_DURATION 2.0f
#define DISSOLVE_DURATION 0.5f
#define MESH_SEGMENTS 5

typedef enum {
    STATE_CASTING,
    STATE_RISING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    float stateTimer;
    float radius;
    float maxHeight;
    float currentHeight;
    float yawOffset;
    float scaleVariance;
    int ownerAgentId;
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "{{name}}_skill_params.inl"

void Init{{Name}}Skill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;

#define {{NAME}}_TUNABLE_COUNT 2
    static SkillTunableEntry s_tunables[{{NAME}}_TUNABLE_COUNT];
    int tn = 0;
#include "{{name}}_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("{{NAME}}");
    SkillTunables_LoadPersisted("skills/{{element}}/{{name}}_skill/{{name}}_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void Cast{{Name}}Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = target,
            .stateTimer = 0.0f,
            .radius = 14.0f * params.sizeScale,
            .maxHeight = 80.0f * params.sizeScale,
            .currentHeight = 0.0f,
            .yawOffset = (float)GetRandomValue(0, 360),
            .scaleVariance = (float)GetRandomValue(85, 115) / 100.0f,
            .ownerAgentId = agentId,
            .active = true
        };
        SpawnCastEffect(startPos, EFFECT_PRESET_EARTH_CRACK, params.sizeScale * 0.6f);
        return;
    }
}

void Update{{Name}}Skill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= 0.3f) {
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
                    SpawnImpactEffect(s->position, EFFECT_PRESET_EARTH_CRACK, 1.0f);
                    ApplyAoEDamage(s->position, s->radius * 1.5f, 25.0f, 0.0f);
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

        float dirYaw = s->yawOffset * DEG2RAD;
        Vector3 dir = { cosf(dirYaw), 0.0f, sinf(dirYaw) };
        Vector3 perp = { -dir.z, 0.0f, dir.x };
        float segHeight = (s->currentHeight / MESH_SEGMENTS) * s->scaleVariance;
        float baseRadius = s->radius * s->scaleVariance;

        Vector3 segBottom = s->position;
        for (int seg = 0; seg < MESH_SEGMENTS; seg++) {
            float segT = (float)seg / (float)(MESH_SEGMENTS - 1);
            float jitter = (float)GetRandomValue(-30, 30) / 10.0f;
            Vector3 jitteredBottom = Vector3Add(segBottom, Vector3Scale(perp, jitter));
            Vector3 segTop = { jitteredBottom.x, segBottom.y + segHeight, jitteredBottom.z };
            float jitterTop = (float)GetRandomValue(-30, 30) / 10.0f;
            segTop = Vector3Add(segTop, Vector3Scale(perp, jitterTop - jitter));

            float radiusBottom = Math_Mix(baseRadius, baseRadius * 0.6f, segT);
            float radiusTop = Math_Mix(baseRadius, baseRadius * 0.6f, segT + 1.0f / MESH_SEGMENTS);
            DrawCoreCylinder(jitteredBottom, segTop, radiusBottom, radiusTop, 12, ELEMENT_COLOR_EARTH);

            segBottom = segTop;
        }
    }
}

void Unload{{Name}}Skill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

SKILL_EMPTY_PROJECTILE_API({{Name}})
