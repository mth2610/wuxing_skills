#include "{{name}}_skill.h"
#include "core/particle_system.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include <math.h>

#define MAX_INSTANCES 16

typedef enum {
    STATE_CASTING,
    STATE_FLYING,
    STATE_IMPACT,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    Vector3 target;
    Vector3 velocity;
    float stateTimer;
    float radius;
    int ownerAgentId;   // caster's agent slot, set at cast time (ownership tracking)
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void Init{{Name}}Skill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void Cast{{Name}}Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = startPos,
            .target = target,
            .velocity = (Vector3){0},
            .stateTimer = 0.0f,
            .radius = 8.0f * params.sizeScale,
            .ownerAgentId = agentId,
            .active = true
        };
        SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, params.sizeScale * 0.6f);
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
                if (s->stateTimer >= 0.35f) {
                    s->state = STATE_FLYING;
                    s->stateTimer = 0.0f;
                    s->velocity = Vector3Scale(Vector3Normalize(Vector3Subtract(s->target, s->position)), 220.0f);
                }
                break;

            case STATE_FLYING:
                s->position = Vector3Add(s->position, Vector3Scale(s->velocity, dt));
                if (Vector3Distance(s->position, s->target) < s->radius + enemyRadius) {
                    s->state = STATE_IMPACT;
                    s->stateTimer = 0.0f;
                    SpawnImpactEffect(s->position, EFFECT_PRESET_FIRE_EXPLOSION, 1.0f);
                    ApplyAoEDamage(s->position, 30.0f, 25.0f, 0.0f);
                }
                break;

            case STATE_IMPACT:
                if (s->stateTimer >= 0.2f) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE:
                if (s->stateTimer >= 0.5f) {
                    s->active = false;
                }
                break;
        }
    }
}

void Draw{{Name}}Skill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        if (s->state != STATE_FLYING) continue;
        DrawEffectMesh(MESH_PRESET_SPHERE, s->position, (Vector3){ s->radius, s->radius, s->radius }, ELEMENT_COLOR_FIRE);
    }
}

void Unload{{Name}}Skill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

bool Is{{Name}}SkillCoiling(void) {
    return false;
}

int Get{{Name}}SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES && count < maxProjectiles; i++) {
        if (!s_instances[i].active || s_instances[i].state != STATE_FLYING) continue;
        outProjectiles[count].position = s_instances[i].position;
        outProjectiles[count].radius = s_instances[i].radius;
        outProjectiles[count].active = true;
        count++;
    }
    return count;
}

void Deactivate{{Name}}Projectile(int index) {
    if (index < 0 || index >= MAX_INSTANCES) return;
    s_instances[index].active = false;
}
