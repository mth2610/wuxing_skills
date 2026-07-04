#include "magma_fissure_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/path_spline.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/force_field.h"
#include "core/tuning.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define MAX_INSTANCES 32
#define MESH_SEGMENTS 4

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
    float yawOffset;
    float scaleVariance;
    int ownerAgentId;
    bool active;
    bool decalSpawned;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "magma_fissure_skill_params.inl"

static Texture2D s_crackTex;
static ColorGradient s_fireGrad;
static ForceField s_updraftField;

static void RebuildUpdraftField(void) {
    ForceField_Clear(&s_updraftField);
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = s_updraftStrength
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = s_noiseStrength,
        .noiseScale = s_noiseScale,
        .noiseSpeed = s_noiseSpeed
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_NOISE_PERLIN,
        .strength = s_perlinStrength,
        .noiseScale = s_noiseScale, // Dùng chung scale/speed với Curl cho gọn UI
        .noiseSpeed = s_noiseSpeed
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_VORTEX_AXIS,
        .strength = s_vortexStrength,
        .direction = {0, 1, 0}
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = s_dragStrength
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_WIND,
        .direction = {1, 0, 0},
        .strength = s_windStrength
    });
    ForceField_AddLayer(&s_updraftField, (ForceLayer){
        .type = FORCE_VISCOSITY,
        .strength = s_viscosityStrength
    });
}

void InitMagmaFissureSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
    
    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");

    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.00f, (Color){ 255, 255, 220, 255 }); // lint: allow-color
    ColorGradient_AddStop(&s_fireGrad, 0.15f, (Color){ 255, 170, 20, 255 });  // lint: allow-color
    ColorGradient_AddStop(&s_fireGrad, 0.50f, (Color){ 220, 40, 5, 200 });    // lint: allow-color
    ColorGradient_AddStop(&s_fireGrad, 1.00f, (Color){ 30, 30, 30, 0 });      // lint: allow-color

    RebuildUpdraftField();

#define MAGMA_FISSURE_TUNABLE_COUNT 19
    static SkillTunableEntry s_tunables[MAGMA_FISSURE_TUNABLE_COUNT];
    int tn = 0;
#include "magma_fissure_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("MAGMA_FISSURE");
    SkillTunables_LoadPersisted("skills/fire/magma_fissure_skill/magma_fissure_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void CastMagmaFissureSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    Vector3 rawPath[33];
    int rawCount = 0;
    if (params.pathPointCount > 1) {
        for (int i = 0; i < params.pathPointCount && i < 32; i++) rawPath[rawCount++] = params.pathPoints[i];
    } else {
        rawPath[rawCount++] = startPos;
        rawPath[rawCount++] = target;
    }

    Vector3 sampled[MAX_INSTANCES];
    int sampledCount = SamplePath(rawPath, rawCount, s_instanceSpacing, sampled, MAX_INSTANCES);
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
            .waitTime = (float)p / (float)sampledCount * s_staggerDuration,
            .radius = s_baseRadius * params.sizeScale,
            .maxHeight = s_maxHeight * params.sizeScale,
            .currentHeight = 0.0f,
            .ownerAgentId = agentId,
            .active = true,
            .decalSpawned = false,
            .scaleVariance = (float)GetRandomValue(85, 115) / 100.0f,
            .yawOffset = (float)GetRandomValue(0, 360)
        };
    }
    SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, params.sizeScale * 0.6f);
}

void UpdateMagmaFissureSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    RebuildUpdraftField(); // Áp dụng ngay nếu người chơi kéo slider trên UI
    
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
                if (!s->decalSpawned && s->stateTimer >= s->waitTime * 0.5f) {
                    Vector3 decalPos = { s->position.x, s->position.y + 0.02f, s->position.z };
                    DecalSystem_Add(decalPos, (float)(rand() % 360), s->radius * 3.0f, s_crackTex, s_activeDuration + s_risingDuration + s_dissolveDuration, ColorAlpha(ELEMENT_COLOR_FIRE, 0.9f));
                    s->decalSpawned = true;
                }
                if (s->stateTimer >= s->waitTime) {
                    s->state = STATE_RISING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_RISING: {
                float t = SmoothStep01(s->stateTimer / s_risingDuration);
                s->currentHeight = Math_Mix(0.0f, s->maxHeight, t);
                if (s->stateTimer >= s_risingDuration) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    s->currentHeight = s->maxHeight;
                    
                    SpawnImpactEffect(s->position, EFFECT_PRESET_FIRE_EXPLOSION, 1.0f);
                    ApplyAoEDamage(s->position, s->radius * 1.5f, 25.0f, 2.0f);
                    VFXLight_Spawn(s->position, ELEMENT_COLOR_FIRE, s->radius * s_lightRadiusMult, 0.8f, VFX_PRIORITY_LOW);
                    
                    if (s_cameraShake > 0.01f) {
                        CameraFX_Shake(s_cameraShake);
                    }
                }
                break;
            }

            case STATE_ACTIVE:
                // Tăng mật độ hạt (particle) để tạo thành cột dung nham phun trào thay vì dùng Mesh cứng đơ
                for (int k = 0; k < 4; k++) {
                    float tRand = (float)rand() / (float)RAND_MAX;
                    bool isSpark = (tRand > 0.7f); // 30% là tia lửa nhỏ bay nhanh, 70% là cục dung nham to bay chậm

                    ParticleConfig cfg = {
                        .position = {
                            s->position.x + ((float)rand() / (float)RAND_MAX - 0.5f) * s->radius * 1.5f,
                            s->position.y + ((float)rand() / (float)RAND_MAX) * 0.1f,
                            s->position.z + ((float)rand() / (float)RAND_MAX - 0.5f) * s->radius * 1.5f
                        },
                        .velocity = { 
                            ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f * s_particleSpeedMult,
                            (isSpark ? (3.0f + ((float)rand() / (float)RAND_MAX) * 2.0f) : (1.5f + ((float)rand() / (float)RAND_MAX) * 1.0f)) * s_particleSpeedMult,
                            ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f * s_particleSpeedMult
                        },
                        .radius = isSpark ? (0.02f + ((float)rand() / (float)RAND_MAX) * 0.02f) : (0.08f + ((float)rand() / (float)RAND_MAX) * 0.08f),
                        .lifetime = isSpark ? 0.4f : (0.6f + ((float)rand() / (float)RAND_MAX) * 0.5f),
                        .forceField = &s_updraftField,
                        .gradient = &s_fireGrad
                    };
                    SpawnParticle(cfg);
                }
            
                // Ánh sáng bập bùng liên tục
                if (rand() % 100 < 15) {
                    VFXLight_Spawn(s->position, ELEMENT_COLOR_FIRE, s->radius * s_lightRadiusMult, 0.15f, VFX_PRIORITY_LOW);
                }

                if (s->stateTimer >= s_activeDuration) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE: {
                float t = SmoothStep01(s->stateTimer / s_dissolveDuration);
                s->currentHeight = Math_Mix(s->maxHeight, 0.0f, t);
                if (s->stateTimer >= s_dissolveDuration) {
                    s->active = false;
                }
                break;
            }
        }
    }
}

void DrawMagmaFissureSkill(void) {
    // Kỹ năng này hoàn toàn dùng hệ thống hạt (Particle System) và Decal
    // nên không cần vẽ Mesh hình trụ cứng đơ nữa. Việc vẽ hạt được Engine
    // tự động lo (trong ParticleSystem_Draw).
}

void UnloadMagmaFissureSkill(void) {
    // Managed by ResourceManager
}

SKILL_EMPTY_PROJECTILE_API(MagmaFissure)
