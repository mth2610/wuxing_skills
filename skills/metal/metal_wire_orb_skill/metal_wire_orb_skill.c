#include "metal_wire_orb_skill.h"
#include "../../../core/skill_helper.h"
#include "../../../core/trail_system.h"
#include "../../../core/particle_system.h"
#include "../../../core/vfx_light.h"
#include "../../../core/color_gradient.h"
#include "../../../core/skill_manager.h"
#include "raymath.h"
#include <stdlib.h>

#include "metal_wire_orb_skill_params.inl"

static ColorGradient s_metalGradient;
static Matrix s_orbMatrix = {0};
static Vector3 s_orbVelocity = {0};
static bool s_orbActive = false;
static float s_spawnTimer = 0.0f;
static float s_globalPhase = 0.0f;
void InitMetalWireOrbSkill(int screenWidth, int screenHeight) {
    static SkillTunableEntry s_tunables[METAL_WIRE_ORB_TUNABLE_COUNT];
    int tn = 0;
#include "metal_wire_orb_skill_tunables.inl"

    SkillTunables_LoadPersisted("skills/metal/metal_wire_orb_skill/metal_wire_orb_skill.tuning", s_tunables, tn);

    s_metalGradient.count = 3;
    s_metalGradient.stops[0] = (GradientStop){0.0f, WHITE};
    s_metalGradient.stops[1] = (GradientStop){0.5f, (Color){255, 215, 0, 255}};
    s_metalGradient.stops[2] = (GradientStop){1.0f, (Color){255, 140, 0, 0}};
    
    int s_skillIndex = Skill_GetIndexByName("METAL_WIRE_ORB");
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);

    ForceField_Clear(&s_wireForce);
    SkillForceMix_AddLayers(&s_wireForceMix, &s_wireForce);
}

void CastMetalWireOrbSkill(int agentId, Vector3 pos, Vector3 target, SkillParams params) {
    (void)agentId;
    (void)params;
    s_orbActive = true;
    s_orbMatrix = MatrixTranslate(pos.x, pos.y, pos.z);
    
    Vector3 dir = Vector3Subtract(target, pos);
    if (Vector3LengthSqr(dir) > 1e-6f) {
        dir = Vector3Normalize(dir);
    } else {
        dir = (Vector3){0, 0, 1};
    }
    s_orbVelocity = Vector3Scale(dir, tp_orbSpeed);
    s_spawnTimer = 0.0f;
    s_globalPhase = 0.0f;
}

void UpdateMetalWireOrbSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    if (!s_orbActive) return;
    
    // Move the orb
    s_orbMatrix.m12 += s_orbVelocity.x * dt;
    s_orbMatrix.m13 += s_orbVelocity.y * dt;
    s_orbMatrix.m14 += s_orbVelocity.z * dt;

    ForceField_Clear(&s_wireForce);
    SkillForceMix_AddLayers(&s_wireForceMix, &s_wireForce);
    
    s_globalPhase += 10.0f * dt; // Base phase advancement
    
    s_spawnTimer += dt;
    if (s_spawnTimer >= tp_wireSpawnInterval) {
        s_spawnTimer = 0.0f;
        
        // Spawn 2-4 wires per burst
        int numWires = 2 + (GetRandomValue(0, 100) % 3);
        for (int i = 0; i < numWires; i++) {
            TrailConfig cfg = {0};
            cfg.type = TRAIL_TYPE_FOLLOWER;
            
            float life = tp_wireLifeMin + (float)GetRandomValue(0, 1000) / 1000.0f * (tp_wireLifeMax - tp_wireLifeMin);
            float len = tp_wireLengthMin + (float)GetRandomValue(0, 1000) / 1000.0f * (tp_wireLengthMax - tp_wireLengthMin);
            float thick = tp_wireThickMin + (float)GetRandomValue(0, 1000) / 1000.0f * (tp_wireThickMax - tp_wireThickMin);
            
            cfg.life = life;
            cfg.trailLength = len;
            cfg.thick = thick;
            cfg.gradient = &s_metalGradient;
            cfg.priority = VFX_PRIORITY_LOW;
            cfg.forceField = &s_wireForce;
            
            int id = SpawnTrailEntity(cfg);
            if (id != -1) {
                Trail_AttachToTransform(id, &s_orbMatrix, (Vector3){0,0,0});
                
                float radius = tp_wireOrbitRadiusMin + (float)GetRandomValue(0, 1000) / 1000.0f * (tp_wireOrbitRadiusMax - tp_wireOrbitRadiusMin);
                float speed = tp_wireOrbitSpeedMin + (float)GetRandomValue(0, 1000) / 1000.0f * (tp_wireOrbitSpeedMax - tp_wireOrbitSpeedMin);
                
                // Random axis, but mostly aligned with velocity direction or orthogonal to it
                Vector3 axis = {
                    (float)GetRandomValue(-100, 100),
                    (float)GetRandomValue(-100, 100),
                    (float)GetRandomValue(-100, 100)
                };
                if (Vector3LengthSqr(axis) < 1e-6f) axis = (Vector3){0, 1, 0};
                axis = Vector3Normalize(axis);
                
                // Add some random phase offset so they don't spawn all clumped
                float phase = s_globalPhase + (float)GetRandomValue(0, 314) / 100.0f;
                
                Trail_SetFollowerOrbit(id, radius, speed, axis, phase);
            }
        }
    }
    
    // Deactivate if out of bounds (approximate)
    if (Vector3LengthSqr((Vector3){s_orbMatrix.m12, s_orbMatrix.m13, s_orbMatrix.m14}) > 100000.0f) {
        s_orbActive = false;
    }
}

void DrawMetalWireOrbSkill(void) {
    if (!s_orbActive) return;
    
    Vector3 pos = {s_orbMatrix.m12, s_orbMatrix.m13, s_orbMatrix.m14};
    
    // Draw the core of the orb
    DrawSphere(pos, tp_orbRadius, (Color){255, 230, 150, 200});
    
    // Light
    VFXLight_Spawn(pos, (Color){255, 215, 0, 255}, tp_orbRadius * 5.0f, 0.1f, VFX_PRIORITY_LOW);
}

void UnloadMetalWireOrbSkill(void) {
    s_orbActive = false;
}

bool IsMetalWireOrbSkillCoiling(void) {
    return false;
}

int GetMetalWireOrbSkillProjectiles(Vector3 *positions, int maxCount) {
    if (s_orbActive && maxCount > 0) {
        positions[0] = (Vector3){s_orbMatrix.m12, s_orbMatrix.m13, s_orbMatrix.m14};
        return 1;
    }
    return 0;
}

void DeactivateMetalWireOrbSkillProjectile(int index) {
    if (index == 0) s_orbActive = false;
}
