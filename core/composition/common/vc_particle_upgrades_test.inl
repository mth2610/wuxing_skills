#include "core/particles/particle_system.h"
#include "core/resource_manager.h"
#include "core/force_field.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static ForceField s_testGravityField;
static bool s_testGravityInit = false;

// Guided-travel diagnostic.  The route, moving target, force field and impact
// template are pointer-backed, so every active cast owns stable storage until
// its longest parent particle has expired.  The pool is intentionally small:
// this fixture is a one-shot visual proof, not a gameplay emitter farm.
#define GUIDED_PARTICLE_TEST_MAX 4
#define GUIDED_PARTICLE_TEST_POINTS 6

typedef struct GuidedParticleTestState {
    bool active;
    float age;
    Vector3 source;
    Vector3 target;
    Vector3 points[GUIDED_PARTICLE_TEST_POINTS];
    ParticleTravelPath path;
    ForceField field;
    ForceField impactField;
    ParticleEmitterHandle meshEmitter;
    ParticleEmitterHandle pointEmitter;
} GuidedParticleTestState;

static GuidedParticleTestState s_guidedParticleTests[GUIDED_PARTICLE_TEST_MAX];
static MeshAdjacency s_guidedParticleSourceMesh;
static bool s_guidedParticleSharedInit = false;

static void GuidedParticleTest_InitShared(void)
{
    if (s_guidedParticleSharedInit) return;

    // A compact sphere keeps the flight formation readable as one moving orb.
    // Build adjacency once; every cast only supplies a cheap world transform.
    Mesh source = GenMeshSphere(0.70f, 16, 10);
    MeshAdjacency_Build(&s_guidedParticleSourceMesh, source);
    UnloadMesh(source);

    s_guidedParticleSharedInit = true;
}

static void GuidedParticleTest_Clear(GuidedParticleTestState *state)
{
    if (state->meshEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(state->meshEmitter);
    if (state->pointEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(state->pointEmitter);
    state->meshEmitter = PARTICLE_EMITTER_INVALID;
    state->pointEmitter = PARTICLE_EMITTER_INVALID;
    state->active = false;
}

static GuidedParticleTestState *GuidedParticleTest_Allocate(void)
{
    for (int i = 0; i < GUIDED_PARTICLE_TEST_MAX; ++i) {
        if (!s_guidedParticleTests[i].active) return &s_guidedParticleTests[i];
    }
    return NULL;
}

static void GuidedParticleTest_Spawn(Vector3 source, Vector3 target)
{
    GuidedParticleTest_InitShared();
    GuidedParticleTestState *state = GuidedParticleTest_Allocate();
    if (!state) return;

    *state = (GuidedParticleTestState){
        .active = true,
        .meshEmitter = PARTICLE_EMITTER_INVALID,
        .pointEmitter = PARTICLE_EMITTER_INVALID,
    };
    state->target = target;
    state->source = source;
    Vector3 span = Vector3Subtract(target, source);
    float spanLength = Vector3Length(span);
    Vector3 forward = spanLength > 0.001f ? Vector3Scale(span, 1.0f / spanLength)
                                         : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 lateral = Vector3Normalize(Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, forward));
    if (Vector3LengthSqr(lateral) < 0.001f) lateral = (Vector3){0.0f, 0.0f, 1.0f};
    // Sample one cubic Bezier spline. The travel solver consumes line
    // segments, so these samples approximate one smooth route.
    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 controlA = Vector3Add(source,
        Vector3Add(Vector3Scale(forward, spanLength * 0.30f),
                   Vector3Add(Vector3Scale(lateral, spanLength * 0.22f),
                              Vector3Scale(up, spanLength * 0.10f))));
    Vector3 controlB = Vector3Add(source,
        Vector3Add(Vector3Scale(forward, spanLength * 0.70f),
                   Vector3Add(Vector3Scale(lateral, -spanLength * 0.22f),
                              Vector3Scale(up, spanLength * 0.06f))));
    const float t[GUIDED_PARTICLE_TEST_POINTS] = {0.10f, 0.24f, 0.40f, 0.58f, 0.76f, 0.90f};
    for (int i = 0; i < GUIDED_PARTICLE_TEST_POINTS; ++i) {
        float u = 1.0f - t[i];
        float b0 = u * u * u;
        float b1 = 3.0f * u * u * t[i];
        float b2 = 3.0f * u * t[i] * t[i];
        float b3 = t[i] * t[i] * t[i];
        state->points[i] = (Vector3){
            source.x * b0 + controlA.x * b1 + controlB.x * b2 + target.x * b3,
            source.y * b0 + controlA.y * b1 + controlB.y * b2 + target.y * b3,
            source.z * b0 + controlA.z * b1 + controlB.z * b2 + target.z * b3,
        };
    }
    state->path = (ParticleTravelPath){
        .points = state->points,
        .pointCount = GUIDED_PARTICLE_TEST_POINTS,
        .target = &state->target,
        .formationOrigin = &state->source,
        .speed = 4.0f,
        .steering = 3.0f,
        .maxAcceleration = 5.0f,
        .waypointRadius = 0.22f,
        .targetRadius = 0.12f,
        .arrivalForceField = &state->impactField,
        // Snap the impact phase to the actual click target; the radial force
        // should be the visible separation, not an authored positional bias.
        .arrivalOffset = 0.0f,
        .arrivalKick = 0.0f,
        .arrivalVelocityScale = 0.001f,
        .arrivalForceDuration = 0.18f,
    };

    ForceField_Clear(&state->field);
    ForceField_AddLayer(&state->field, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 1.60f,
        .noiseScale = 2.40f,
        .noiseSpeed = 0.90f,
    });

    ForceField_Clear(&state->impactField);
    ForceField_AddLayer(&state->impactField, (ForceLayer){
        .type = FORCE_GRAVITY_POINT,
        .origin = target,
        .strength = -3.0f,
        .radius = 3.5f,
        .falloff = 1.0f,
    });
    ForceField_AddLayer(&state->impactField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 6.00f,
        .noiseScale = 4.50f,
        .noiseSpeed = 2.8f,
    });
    ForceField_AddLayer(&state->impactField, (ForceLayer){
        .type = FORCE_VISCOSITY,
        .strength = 6.00f,
    });

    ParticleConfig follower = {
        .position = source,
        .velocity = (Vector3){1.2f, 0.25f, 0.0f},
        .lifetime = 7.0f,
        .radius = 0.105f,
        .colorStart = (Color){90, 235, 255, 245},
        .colorEnd = (Color){130, 70, 255, 0},
        .forceField = &state->field,
        .travelPath = &state->path,
        .stretchStrength = 0.014f,
        .stretchMinSpeed = 0.35f,
        .render.blendMode = VFX_BLEND_ADDITIVE,
        .render.unlit = 1,
        .render.emissiveBoost = 10.0f,
    };
    Matrix sourceTransform = MatrixMultiply(
        MatrixScale(0.1375f, 0.1375f, 0.1375f),
        MatrixTranslate(source.x, source.y, source.z));
    ParticleEmitterDesc meshDesc = {
        .simulationPolicy = PARTICLE_SIM_AUTO,
        .renderMode = PARTICLE_RENDER_BILLBOARD,
        .particle = follower,
        .moduleFlags = PARTICLE_MODULE_FORCE_FIELD |
                       PARTICLE_MODULE_VELOCITY_STRETCH |
                       PARTICLE_MODULE_PATH_FOLLOW,
        .debugName = "Guided test mesh source",
        .source = {
            .type = PARTICLE_SOURCE_MESH_EDGE,
            .mesh = &s_guidedParticleSourceMesh,
            .transform = sourceTransform,
        },
    };
    state->meshEmitter = ParticleManager_CreateEmitter(&meshDesc);
    if (state->meshEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_Emit(state->meshEmitter, 512);

    // One larger leader starts from an exact point and makes the second source
    // mode easy to distinguish from the surrounding mesh-sampled formation.
    ParticleEmitterDesc pointDesc = meshDesc;
    pointDesc.debugName = "Guided test point source";
    pointDesc.source = (ParticleEmissionSource){
        .type = PARTICLE_SOURCE_POINT,
        .point = source,
    };
    pointDesc.particle.radius = 0.22f;
    pointDesc.particle.colorStart = (Color){255, 255, 255, 255};
    pointDesc.particle.colorEnd = (Color){50, 205, 255, 0};
    pointDesc.particle.render.emissiveBoost = 12.0f;
    state->pointEmitter = ParticleManager_CreateEmitter(&pointDesc);
    if (state->pointEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_Emit(state->pointEmitter, 1);

    if (state->meshEmitter == PARTICLE_EMITTER_INVALID &&
        state->pointEmitter == PARTICLE_EMITTER_INVALID)
        GuidedParticleTest_Clear(state);
}

static void GuidedParticleTest_Update(float dt)
{
    for (int i = 0; i < GUIDED_PARTICLE_TEST_MAX; ++i) {
        GuidedParticleTestState *state = &s_guidedParticleTests[i];
        if (!state->active) continue;
        state->age += dt;
        state->impactField.layers[0].origin = state->target;
        // Parent lifetime is 7.0 s.  Keep pointer-backed route data alive past
        // that bound; the same particles own the impact phase until expiry.
        if (state->age >= 7.25f) GuidedParticleTest_Clear(state);
    }
}

// Public one-shot entry so sync_vfx_test.py exposes this diagnostic as its own
// NEW FX button. The internal name above intentionally remains test-specific.
void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target)
{
    GuidedParticleTest_Spawn(source, target);
}

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
    GuidedParticleTest_Spawn(Vector3Add(pos, (Vector3){-2.7f, 0.65f, 0.0f}),
                             Vector3Add(pos, (Vector3){2.7f, 0.85f, 0.0f}));
    
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
