// Ember burst primary — NE_SparkDebris two-tier cascade
// (docs/vfx_breakdowns/01_fx_explosions.md §2.3 + 03_fx_sparks_smoke_fire.md §1).
//
// Event API: call exactly once. Each primary is one HDR capsule sprite: the
// texture owns its white core, orange rim, and soft falloff. On floor
// bounce the body sheds 2 short-lived secondary sparks (the doc's
// NE_SecondarySparks tier).

#include "core/vfx_light.h"

#define EMBER_BURST_MIN_COUNT 30
#define EMBER_BURST_MAX_COUNT 60
#define EMBER_BURST_CONE_RAD 1.15f
#define EMBER_BURST_BOUNCE 0.35f
#define EMBER_BURST_SECONDARIES 1

static ForceField s_emberBurstField;
static bool s_emberBurstReady = false;
static const ParticleDynamicsProfile s_emberBurstDynamics = {
    /* Effective 0.25 kg carrier: a compact VFX unit, not literal ash mass.
     * It converts an authored N*s launch impulse into a short 4-8 m/s arc. */
    .inverseMassKg = 4.0f, .gravityScale = 1.0f, .linearDragPerSecond = 3.2f,
    .terminalSpeedMps = 7.0f, .windCouplingHz = 1.0f, .windSusceptibility = 0.12f,
};

// Short hot fragment shed on bounce. Static storage: the particle system keeps
// a copy of the template, and nested on-collision templates are stripped on
// spawn, so this tier cannot recurse.
static ParticleConfig s_emberBurstSecondary;

static void EmberBurst_Init(void)
{
    if (s_emberBurstReady) return;
    ForceField_Clear(&s_emberBurstField);
    ForceField_AddLayer(&s_emberBurstField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 0.40f,
        .noiseScale = 2.0f,
    });
    s_emberBurstSecondary = (ParticleConfig){
        .radius = 0.006f,
        .lifetime = 0.25f,
        .colorStart = WHITE,
        .colorEnd = (Color){255, 140, 40, 0},
        .physics.collisionEnabled = false,
        .render.texture = ParticleSystem_SparkCapsuleSprite(),
        .render.blendMode = VFX_BLEND_ADDITIVE,
        .render.unlit = 1,
        .render.emissiveBoost = 4.0f,
        .render.stretchStrength = 0.08f,
        .render.stretchMinSpeed = 0.5f,
        .render.facingMode = VFX_FACING_VELOCITY,
        .render.trailLength = 0,
    };
    s_emberBurstReady = true;
}

void VFX_ComposeEmberBurst(Vector3 pos, Vector3 normal,
                           VC_MaterialId matId, float scale,
                           float severity01)
{
    if (scale <= 0.0f) return;
    EmberBurst_Init();

    float normalLen2 = Vector3DotProduct(normal, normal);
    if (normalLen2 < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    else normal = Vector3Scale(normal, 1.0f / sqrtf(normalLen2));
    severity01 = Clamp(severity01, 0.0f, 1.0f);

    int count = EMBER_BURST_MIN_COUNT +
                (int)((float)(EMBER_BURST_MAX_COUNT - EMBER_BURST_MIN_COUNT) *
                      severity01 + 0.5f);
    const VFX_ElementMaterial *material = VFX_Material(matId);
    Vector3 origin = Vector3Add(pos, Vector3Scale(normal, 0.035f * scale));
    Texture2D sparkTex = ParticleSystem_SparkCapsuleSprite();

    for (int i = 0; i < count; ++i)
    {
        Vector3 dir = VC_DirConeUniform(normal, EMBER_BURST_CONE_RAD,
                                        Random01(), Random01());
        float speed = Math_Mix(4.0f, 8.0f, Random01()) * scale;
        Vector3 velocity = Vector3Scale(dir, speed);
        float radius = Math_Mix(0.018f, 0.028f, Random01()) * scale;
        float lifetime = Math_Mix(0.65f, 1.20f, Random01());

        ParticleConfig body = (ParticleConfig){
            .position = origin,
            .velocity = (Vector3){0},
            .radius = radius,
            .lifetime = lifetime,
            .forceField = &s_emberBurstField,
            .windInfluence = 0.12f,
            .physics.dynamics = &s_emberBurstDynamics,
            .physics.initialImpulseNs = Vector3Scale(velocity, 0.25f),
            .colorStart = WHITE,
            .colorEnd = (Color){255, 120, 20, 0},
            .physics.collisionEnabled = true,
            .physics.collisionElasticity = EMBER_BURST_BOUNCE,
            .physics.collisionFloorY = pos.y,
            .physics.onCollisionEmit = &s_emberBurstSecondary,
            .physics.onCollisionEmitCount = EMBER_BURST_SECONDARIES,
            .render.texture = sparkTex,
            .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1,
            .render.contrastProfile = VFX_CONTRAST_FIRE,
            .render.emissiveBoost = 4.5f,
            .render.stretchStrength = 0.10f,
            .render.stretchMinSpeed = 1.0f,
            .render.facingMode = VFX_FACING_VELOCITY,
            .render.trailLength = 0,
        };

        SpawnParticle(body);
    }

    // Optical punch for the first frames; the embers themselves carry the tail.
    VFXLight_Spawn(origin, material->glow,
                   4.0f * scale * (0.5f + 0.5f * severity01), 0.24f,
                   VFX_PRIORITY_LOW);
}
