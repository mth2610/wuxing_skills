#include "boulder_barrage_skill.h"
#include "core/particle_system.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/resource_manager.h"
#include "core/presets/vfx_presets.h"
#include "core/material/material_system.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/geometry/mesh_cache.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/screen_distort.h"
#include "entities/entities.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>

#define MAX_INSTANCES 16

typedef enum {
    BOULDER_STATE_RISING,   // Staggered rise from ground
    BOULDER_STATE_HOVERING, // Natural floating oscillation
    BOULDER_STATE_FLYING,   // Flying toward target
    BOULDER_STATE_IMPACTED  // Hit target/ground
} BoulderState;

typedef struct {
    BoulderState state;
    Vector3 position;
    Vector3 spawnPos;
    Vector3 hoverPos;
    Vector3 velocity;
    float scale;
    float delayTimer;       // rise delay
    float flightDelay;      // fly delay
    float elapsed;
    float hoverOscOffset;
    int seed;
    bool active;
    bool puffSpawned;       // smoke puff on eruption
} BoulderSub;

typedef struct {
    Vector3 casterStartPos;
    Vector3 targetPos;
    BoulderSub boulders[8];
    int boulderCount;
    float elapsed;
    int ownerAgentId;
    bool active;
    float sizeScale;
    float damage;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "boulder_barrage_skill_params.inl"

// Spawn a faint dusty earth particle puff
static void SpawnFaintEarthPuff(Vector3 pos, float size) {
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 6;
    cfg.countMax = 10;
    cfg.speedMin = 0.4f;
    cfg.speedMax = 1.0f;
    cfg.radiusMin = size * 0.3f;
    cfg.radiusMax = size * 0.6f;
    cfg.lifetimeMin = 0.5f;
    cfg.lifetimeMax = 0.9f;
    cfg.pitchRange = PI;
    cfg.upwardBias = s_puffUpwardBias;

    // Faint earth/dust color (alpha start is based on s_puffAlpha)
    cfg.colorStart = (Color){135, 115, 90, (unsigned char)s_puffAlpha}; // lint: allow-color
    cfg.colorEnd = (Color){135, 115, 90, 0}; // lint: allow-color

    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 5.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;

    ParticleSystem_SpawnRadialBurst(pos, size, &cfg);
}

// Draw helper using Material + cached Rock Facets
static void DrawCustomBoulder(Vector3 pos, float scale, int seed) {
    EffectMaterial mat = Material_Get(MAT_ROCK);
    
    // 1. Core smooth sphere (very small, only 15% to act as a tiny center fill)
    Material_Begin(mat);
    DrawCoreSphere(pos, scale * 0.15f, 8, 8, WHITE);
    Material_End();

    // 2. Jagged outer rock facet (scale is multiplied by 1.0f so it matches the actual scale of the boulder)
    RockMeshData* data = MeshCache_GetRock(seed, 0.45f); // slightly higher jaggedness 0.45f
    if (data) {
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        Material_Begin(mat);
        
        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlScalef(scale, scale, scale); // 1.0f scale matches actual bounding radius
        ProceduralMesh_DrawRock(data, WHITE);
        rlPopMatrix();
        
        Material_End();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
}

void InitBoulderBarrageSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;

#define BOULDER_BARRAGE_TUNABLE_COUNT 19
    static SkillTunableEntry s_tunables[BOULDER_BARRAGE_TUNABLE_COUNT];
    int tn = 0;
#include "boulder_barrage_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("BOULDER_BARRAGE");
    SkillTunables_LoadPersisted("skills/earth/boulder_barrage_skill/boulder_barrage_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void CastBoulderBarrageSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        
        SkillInstance *s = &s_instances[i];
        s->casterStartPos = startPos;
        s->targetPos = target;
        s->ownerAgentId = agentId;
        s->elapsed = 0.0f;
        s->sizeScale = params.sizeScale;
        s->damage = s_damage;
        
        // Spawn boulders (min to max count)
        int minC = (int)s_minBoulderCount;
        int maxC = (int)s_maxBoulderCount;
        if (minC < 1) minC = 1;
        if (maxC < minC) maxC = minC;
        s->boulderCount = GetRandomValue(minC, maxC);
        
        Vector3 toTarget = Vector3Subtract(target, startPos);
        float distToTarget = Vector3Length(toTarget);
        Vector3 lookDir = (distToTarget > 0.001f) ? Vector3Normalize(toTarget) : (Vector3){0, 0, 1};
        Vector3 right = (Vector3){ -lookDir.z, 0.0f, lookDir.x };
        
        for (int j = 0; j < s->boulderCount; j++) {
            BoulderSub *b = &s->boulders[j];
            
            // Distribute in a semi-circle/V-shape behind and beside the caster
            float angle = -1.2f + (2.4f * j) / (s->boulderCount - 1);
            float cosA = cosf(angle);
            float sinA = sinf(angle);
            Vector3 offsetDir = (Vector3){
                lookDir.x * cosA - lookDir.z * sinA,
                0.0f,
                lookDir.x * sinA + lookDir.z * cosA
            };
            
            // Spawn distance around caster (s_spawnRadiusMin to s_spawnRadiusMax)
            float r = s_spawnRadiusMin + (float)GetRandomValue(0, 100) * 0.01f * (s_spawnRadiusMax - s_spawnRadiusMin);
            Vector3 groundPos = Vector3Add(startPos, Vector3Scale(offsetDir, r));
            groundPos.y = startPos.y; // Align to caster feet level
            
            b->state = BOULDER_STATE_RISING;
            b->scale = (s_minScale + (float)GetRandomValue(0, 100) * 0.01f * (s_maxScale - s_minScale)) * params.sizeScale;
            b->spawnPos = (Vector3){ groundPos.x, groundPos.y - b->scale, groundPos.z }; // start buried
            float hoverH = s_hoverHeightMin + (float)GetRandomValue(0, 100) * 0.01f * (s_hoverHeightMax - s_hoverHeightMin);
            b->hoverPos = (Vector3){ groundPos.x, groundPos.y + hoverH, groundPos.z };
            b->position = b->spawnPos;
            b->velocity = (Vector3){0};
            
            b->delayTimer = j * s_riseDelayStep;                       // rising starts sequentially
            b->flightDelay = s_flightDelayBase + j * s_flightDelayStep; // flying starts sequentially after cast
            b->elapsed = 0.0f;
            b->hoverOscOffset = (float)GetRandomValue(0, 360) * DEG2RAD;
            b->seed = 1000 + j;                      // fixed distinct cache seeds
            b->active = true;
            b->puffSpawned = false;
        }
        
        s->active = true;
        
        // Spawn casting effect at player's feet
        SpawnCastEffect(startPos, EFFECT_PRESET_EARTH_CRACK, params.sizeScale * 0.8f);
        PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
        return;
    }
}

void UpdateBoulderBarrageSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->elapsed += dt;

        bool anyActive = false;
        for (int j = 0; j < s->boulderCount; j++) {
            BoulderSub *b = &s->boulders[j];
            if (!b->active || b->state == BOULDER_STATE_IMPACTED) continue;
            anyActive = true;
            
            b->elapsed += dt;

            // State Machine for each sub-boulder
            if (b->state == BOULDER_STATE_RISING) {
                if (b->elapsed >= b->delayTimer) {
                    if (!b->puffSpawned) {
                        b->puffSpawned = true;
                        // Spawn faint earth cast/dust puff at spawn position
                        SpawnFaintEarthPuff(b->spawnPos, b->scale * 2.0f);
                        PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
                    }
                    float riseTime = s_riseDuration;
                    float progress = (b->elapsed - b->delayTimer) / riseTime;
                    if (progress >= 1.0f) {
                        progress = 1.0f;
                        b->state = BOULDER_STATE_HOVERING;
                        b->elapsed = 0.0f; // reset for hover sine oscillation
                    }
                    b->position = Vector3Lerp(b->spawnPos, b->hoverPos, progress);
                } else {
                    b->position = b->spawnPos; // Keep buried
                }
            }
            else if (b->state == BOULDER_STATE_HOVERING) {
                if (s->elapsed >= b->flightDelay) {
                    b->state = BOULDER_STATE_FLYING;
                    b->elapsed = 0.0f;
                } else {
                    // Slight natural floating oscillation
                    float oscY = sinf(b->elapsed * 4.0f + b->hoverOscOffset) * s_oscYAmplitude;
                    float oscXZ = cosf(b->elapsed * 2.5f + b->hoverOscOffset) * s_oscXZAmplitude;
                    b->position = (Vector3){
                        b->hoverPos.x + oscXZ,
                        b->hoverPos.y + oscY,
                        b->hoverPos.z + oscXZ
                    };
                }
            }
            else if (b->state == BOULDER_STATE_FLYING) {
                Vector3 toTarget = Vector3Subtract(s->targetPos, b->position);
                float dist = Vector3Length(toTarget);
                if (dist > 0.001f) {
                    Vector3 dir = Vector3Normalize(toTarget);
                    // Speed: s_boulderSpeed
                    float step = s_boulderSpeed * dt;
                    if (step >= dist) {
                        b->position = s->targetPos;
                    } else {
                        b->position = Vector3Add(b->position, Vector3Scale(dir, step));
                    }
                } else {
                    b->position = s->targetPos;
                }

                // Check collision with enemy or target
                float distToEnemy = Vector3Distance(b->position, enemyPos);
                float distToTarget = Vector3Distance(b->position, s->targetPos);
                if (distToEnemy <= (b->scale + enemyRadius) || distToTarget <= b->scale || b->position.y < 0.05f) {
                    b->state = BOULDER_STATE_IMPACTED;
                    b->active = false;
                    
                    // Spawn faint earth explosion at impact site
                    SpawnFaintEarthPuff(b->position, b->scale * 3.0f);
                    
                    ScreenDistort_Add(b->position, 0.15f, 0.08f, 0.2f, 0.4f);
                    VFXLight_Spawn(b->position, ELEMENT_COLOR_EARTH, 2.5f, 0.2f, 0);
                    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
                    CameraFX_Shake(s_cameraShake); // Rung màn hình nhẹ khi đá va chạm
                    
                    // Apply AoE Damage to targets
                    Entity_ApplyAoEDamage(b->position, b->scale * 2.2f, s->damage, 1.5f);
                }
            }
        }

        if (!anyActive) {
            s->active = false;
        }
    }
}

void DrawBoulderBarrageSkill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        for (int j = 0; j < s->boulderCount; j++) {
            BoulderSub *b = &s->boulders[j];
            if (!b->active || b->state == BOULDER_STATE_IMPACTED) continue;
            DrawCustomBoulder(b->position, b->scale, b->seed);
        }
    }
}

void UnloadBoulderBarrageSkill(void) {
    // No-op: resources owned by ResourceManager
}

bool IsBoulderBarrageSkillCoiling(void) {
    return false;
}

int GetBoulderBarrageSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!s_instances[i].active) continue;
        for (int j = 0; j < s_instances[i].boulderCount && count < maxProjectiles; j++) {
            BoulderSub *b = &s_instances[i].boulders[j];
            if (!b->active || b->state != BOULDER_STATE_FLYING) continue;
            outProjectiles[count].position = b->position;
            outProjectiles[count].radius = b->scale;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateBoulderBarrageProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!s_instances[i].active) continue;
        for (int j = 0; j < s_instances[i].boulderCount; j++) {
            BoulderSub *b = &s_instances[i].boulders[j];
            if (!b->active || b->state != BOULDER_STATE_FLYING) continue;
            if (count == index) {
                b->active = false;
                b->state = BOULDER_STATE_IMPACTED;
                return;
            }
            count++;
        }
    }
}
