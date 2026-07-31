#include "core/particles/particle_system.h"
#include "core/resource_manager.h"
#include "core/force_field.h"
#include "compute/gpu_particle_system.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static ForceField s_testGravityField;
static bool s_testGravityInit = false;

static void ShardSplash_Init(void) {
    if (s_testGravityInit) return;
    ForceField_Clear(&s_testGravityField);
    ForceLayer grav = {0};
    grav.type = FORCE_GRAVITY_DIR;
    grav.direction = (Vector3){0.0f, -1.0f, 0.0f};
    grav.strength = 9.81f; // Standard Earth gravity
    ForceField_AddLayer(&s_testGravityField, grav);
    s_testGravityInit = true;
}

void VFX_ComposeParticleUpgradesTest(Vector3 pos) {
    ShardSplash_Init();
    
    Vector3 cpuPos = Vector3Subtract(pos, (Vector3){1.5f, 0.0f, 0.0f});
    Vector3 gpuPos = Vector3Add(pos, (Vector3){1.5f, 0.0f, 0.0f});

    // 1. Dust puff config for CPU collision sub-emission
    static ParticleConfig dustConfig;
    static SkillCurve rCurve;
    static bool dustInit = false;
    if (!dustInit) {
        dustConfig = (ParticleConfig){0};
        dustConfig.lifetime = 0.4f;
        dustConfig.radius = 0.15f;
        dustConfig.colorStart = (Color){200, 200, 200, 180};
        dustConfig.colorEnd = (Color){220, 220, 220, 0};
        
        // Add stops to the expansion radius curve
        FloatCurve_AddStop(&rCurve, 0.0f, 0.2f);
        FloatCurve_AddStop(&rCurve, 0.5f, 1.0f);
        FloatCurve_AddStop(&rCurve, 1.0f, 1.2f);
        dustConfig.radiusCurve = &rCurve;
        dustInit = true;
    }

    // 2. Primary CPU fountain particles (Orange/Yellow)
    for (int i = 0; i < 24; i++) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        float speed = GetRandomValue(300, 600) * 0.01f;
        float upwardSpeed = GetRandomValue(400, 800) * 0.01f;

        ParticleConfig cfg = {0};
        cfg.position = cpuPos;
        cfg.velocity = (Vector3){
            cosf(angle) * speed,
            upwardSpeed,
            sinf(angle) * speed
        };
        cfg.lifetime = GetRandomValue(350, 500) * 0.01f;
        cfg.radius = GetRandomValue(8, 14) * 0.01f;
        cfg.colorStart = (Color){255, 180, 50, 255};
        cfg.colorEnd = (Color){255, 50, 0, 0};
        cfg.forceField = &s_testGravityField;

        // Upgrades: Stretch
        cfg.stretchStrength = 0.04f;
        cfg.stretchMinSpeed = 0.3f;

        // Upgrades: Ground Collision
        cfg.collisionEnabled = true;
        cfg.collisionElasticity = 0.55f;
        cfg.collisionFloorY = 0.0f;
        cfg.onCollisionEmit = &dustConfig;
        cfg.onCollisionEmitCount = 4;

        // Upgrades: Particle Trails
        cfg.trailLength = 6;
        cfg.trailWidthRatio = 0.5f;
        cfg.trailColorStart = (Color){255, 220, 120, 255};
        cfg.trailColorEnd = (Color){255, 50, 0, 0};

        SpawnParticle(cfg);
    }

    // 3. Primary GPU fountain particles (Cyan/Blue)
    for (int i = 0; i < 24; i++) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        float speed = GetRandomValue(300, 600) * 0.01f;
        float upwardSpeed = GetRandomValue(400, 800) * 0.01f;

        GpuParticleConfig gcfg = {0};
        gcfg.position = gpuPos;
        gcfg.velocity = (Vector3){
            cosf(angle) * speed,
            upwardSpeed,
            sinf(angle) * speed
        };
        gcfg.lifetime = GetRandomValue(350, 500) * 0.01f;
        gcfg.radius = GetRandomValue(8, 14) * 0.01f;
        gcfg.colorStart = (Color){50, 180, 255, 255};
        gcfg.colorEnd = (Color){0, 50, 255, 0};
        gcfg.forceField = &s_testGravityField;
        gcfg.drag = 0.0f;

        // Upgrades: Stretch
        gcfg.stretchStrength = 0.04f;
        gcfg.stretchMinSpeed = 0.3f;

        // Upgrades: Ground Collision
        gcfg.collisionEnabled = true;
        gcfg.collisionElasticity = 0.55f;
        gcfg.collisionFloorY = 0.0f;

        // HDR Glow boost
        gcfg.emissiveBoost = 4.5f;

        GpuParticleSystem_Spawn(gcfg);
    }
}
