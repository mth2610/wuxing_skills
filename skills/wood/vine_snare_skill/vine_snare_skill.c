#include "skills/wood/vine_snare_skill/vine_snare_skill.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "core/resource_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include "rlgl.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_INSTANCES 16
#define VINES_PER_SNARE 3

typedef enum {
    STATE_CASTING,
    STATE_BURSTING, // Vines shoot up from the ground
    STATE_COILING,  // Actively rooting and dealing tick damage
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    float stateTimer;
    
    float scale;
    float damage;
    
    // Aesthetic randomization offsets
    float baseYawOffsets[VINES_PER_SNARE];
    float heightOffsets[VINES_PER_SNARE];
    float radiusOffsets[VINES_PER_SNARE];
    
    // Mechanics
    float growProgress;
    float dissolveProgress;
    float tickTimer;
    
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];
static Shader s_shader;
static int s_locTime;
static int s_locDissolve;
static int s_locColor;

// Timing configs
#define CASTING_DURATION  0.2f
#define BURSTING_DURATION 0.35f
#define COILING_DURATION  1.8f
#define DISSOLVE_DURATION 0.5f
#define TICK_INTERVAL     0.5f

#ifndef PI
#define PI 3.1415926535f
#endif

void InitVineSnareSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        s_instances[i].active = false;
    }

    s_shader = ResourceManager_LoadShader(
        "skills/wood/vine_snare_skill/vine_snare.vs",
        "skills/wood/vine_snare_skill/vine_snare.fs"
    );

    s_locTime = GetShaderLocation(s_shader, "u_time");
    s_locDissolve = GetShaderLocation(s_shader, "u_dissolve");
    s_locColor = GetShaderLocation(s_shader, "u_color");
}

void CastVineSnareSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        
        SkillInstance *s = &s_instances[i];
        s->state = STATE_CASTING;
        s->position = target;
        s->stateTimer = 0.0f;
        s->scale = params.sizeScale;
        s->damage = params.damage;
        s->growProgress = 0.0f;
        s->dissolveProgress = 0.0f;
        s->tickTimer = 0.0f;
        
        // Randomize the vines to ensure no two snares look mathematically identical
        float baseRot = (float)GetRandomValue(0, 360) * DEG2RAD;
        for (int v = 0; v < VINES_PER_SNARE; v++) {
            // Irregular spacing: ~120 degrees apart, but offset by +-20
            s->baseYawOffsets[v] = baseRot + (v * (PI * 2.0f / VINES_PER_SNARE)) + ((float)GetRandomValue(-20, 20) * DEG2RAD);
            // Irregular heights: 45 to 70 units
            s->heightOffsets[v] = (float)GetRandomValue(45, 70);
            // Irregular widths
            s->radiusOffsets[v] = (float)GetRandomValue(85, 115) / 100.0f;
        }
        
        s->active = true;
        
        // Windup visual at the caster's hands
        SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.7f);
        return;
    }
}

void UpdateVineSnareSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= CASTING_DURATION) {
                    s->state = STATE_BURSTING;
                    s->stateTimer = 0.0f;
                    
                    // VFX Impact at target
                    SpawnImpactEffect(s->position, EFFECT_PRESET_WOOD_BLOOM, s->scale * 1.2f);
                    
                    // Root the targets immediately (speedMult = 0.0f for root duration)
                    float rootRadius = 25.0f * s->scale;
                    Entity_ApplyAoEBuff(s->position, rootRadius, 0.0f, BURSTING_DURATION + COILING_DURATION);
                }
                break;

            case STATE_BURSTING:
                s->growProgress = SmoothStep01(s->stateTimer / BURSTING_DURATION);
                if (s->stateTimer >= BURSTING_DURATION) {
                    s->state = STATE_COILING;
                    s->stateTimer = 0.0f;
                    s->growProgress = 1.0f;
                }
                break;

            case STATE_COILING:
                s->tickTimer += dt;
                // Deal tick damage periodically while coiling
                if (s->tickTimer >= TICK_INTERVAL) {
                    ApplyAoEDamage(s->position, 25.0f * s->scale, s->damage * 0.25f, 0.0f);
                    s->tickTimer -= TICK_INTERVAL;
                }
                
                if (s->stateTimer >= COILING_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE:
                s->dissolveProgress = SmoothStep01(s->stateTimer / DISSOLVE_DURATION);
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
        }
    }
}

void DrawVineSnareSkill(void) {
    bool shaderBound = false;
    
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->state == STATE_CASTING) continue;

        if (!shaderBound) {
            SkillManager_BeginShader(s_shader);
            
            float timeVal = (float)GetTime();
            if (s_locTime >= 0) SetShaderValue(s_shader, s_locTime, &timeVal, SHADER_UNIFORM_FLOAT);
            
            Color woodCol = ELEMENT_COLOR_WOOD;
            Vector4 colorNorm = { (float)woodCol.r/255.0f, (float)woodCol.g/255.0f, (float)woodCol.b/255.0f, 1.0f };
            if (s_locColor >= 0) SetShaderValue(s_shader, s_locColor, &colorNorm, SHADER_UNIFORM_VEC4);
            
            shaderBound = true;
        }

        if (s_locDissolve >= 0) {
            SetShaderValue(s_shader, s_locDissolve, &s->dissolveProgress, SHADER_UNIFORM_FLOAT);
        }

        // Draw the 3 interlocking vines
        for (int v = 0; v < VINES_PER_SNARE; v++) {
            float yaw = s->baseYawOffsets[v];
            float height = s->heightOffsets[v] * s->scale;
            float baseRad = 20.0f * s->scale;
            float tubeWidth = 3.5f * s->scale * s->radiusOffsets[v];
            
            // Build the irregular Bezier path for this vine
            // Starts outward, shoots up and curves heavily inward over the target center
            Vector3 p0 = { s->position.x + cosf(yaw)*baseRad, s->position.y - 5.0f, s->position.z + sinf(yaw)*baseRad };
            Vector3 p1 = { p0.x + cosf(yaw)*10.0f, p0.y + height * 0.4f, p0.z + sinf(yaw)*10.0f };
            Vector3 p2 = { s->position.x + cosf(yaw+1.5f)*baseRad*0.5f, s->position.y + height * 0.8f, s->position.z + sinf(yaw+1.5f)*baseRad*0.5f };
            Vector3 p3 = { s->position.x, s->position.y + height, s->position.z };

            TubeMeshConfig cfg = ProceduralMesh_DefaultTubeConfig();
            // Use core engine deformation for knobbly, organic vines (no perfect cylinders)
            cfg.deform1Amp = 2.5f * s->scale;
            cfg.deform1FreqT = 6.0f;
            cfg.deform1FreqPhi = 3.0f;
            // Taper the tips to points
            cfg.tailTaperMin = 0.0f; 
            cfg.capsuleTailExp = 2.0f;
            
            TubeMeshData tData;
            ProceduralMesh_BuildTube(&tData, p0, p1, p2, p3, tubeWidth, s->growProgress, (float)GetTime(), 16, 8, &cfg);
            
            rlColor4ub(255, 255, 255, 255);
            ProceduralMesh_DrawTube(&tData, 20.0f);
        }
    }

    if (shaderBound) {
        SkillManager_EndShader();
    }
}

void UnloadVineSnareSkill(void) {
    // Rely on ResourceManager to clean up textures and shaders
}

bool IsVineSnareSkillCoiling(void) {
    return false; // Target is CC'd, but the caster doesn't need to channel it
}

int GetVineSnareSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    return 0; // Not a projectile
}

void DeactivateVineSnareProjectile(int index) {
    // No-op
}