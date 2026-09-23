// Generic ground-contact particle ring.  Variants are semantic surface
// vocabularies, not gameplay effects: callers supply position/material/scale.
#include "core/vfx_surface_registry.h"

#define SURFACE_PARTICLE_RING_MIN_PARTICLES 25
#define SURFACE_PARTICLE_RING_MAX_PARTICLES 40
#define SURFACE_PARTICLE_RING_ANIM_RATES 4

static Texture2D s_surfaceParticleRingTex[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT] = {0};
static Texture2D s_surfaceParticleRingNormalTex[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT] = {0};
static SpriteAnim s_surfaceParticleRingAnim[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT][SURFACE_PARTICLE_RING_ANIM_RATES];
static bool s_surfaceParticleRingReady[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT] = {0};
static SkillCurve s_surfaceParticleRingGrow = {0};
static SkillCurve s_surfaceParticleRingFade = {0};
static SkillCurve s_surfaceParticleRingBrake = {0};
static bool s_surfaceParticleRingCurvesReady = false;
static const ParticleDynamicsProfile s_surfaceParticleRingDynamics = {
    // A blast front must preserve enough lateral velocity to travel R1 -> R2.
    // The old dust drag (7/s) stopped the impulse almost at the contact point.
    .inverseMassKg = 1.0f, .gravityScale = 0.05f, .linearDragPerSecond = 1.8f,
    .terminalSpeedMps = 12.0f, .windCouplingHz = 1.4f, .windSusceptibility = 0.65f,
};

static const VFX_SurfaceId s_surfaceParticleRingSurfaces[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT] = {
    VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA,
    VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA,
    VFX_SURFACE_SMOKE_WISPY_NIAGARA,
    VFX_SURFACE_PLASMA_WISPS_NIAGARA,
};

static int SurfaceParticleRing_Index(VFX_SurfaceParticleRingVariant variant)
{
    return (variant >= VFX_SURFACE_PARTICLE_RING_VARIANT_DUST &&
            variant < VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT) ? (int)variant : 0;
}

static void SurfaceParticleRing_Init(VFX_SurfaceParticleRingVariant variant)
{
    const int index = SurfaceParticleRing_Index(variant);
    if (!s_surfaceParticleRingCurvesReady) {
        FloatCurve_AddStop(&s_surfaceParticleRingGrow, 0.0f, 0.72f);
        FloatCurve_AddStop(&s_surfaceParticleRingGrow, 0.34f, 1.15f);
        FloatCurve_AddStop(&s_surfaceParticleRingGrow, 1.0f, 1.38f);
        FloatCurve_AddStop(&s_surfaceParticleRingFade, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_surfaceParticleRingFade, 0.10f, 0.78f);
        FloatCurve_AddStop(&s_surfaceParticleRingFade, 1.0f, 0.0f);
        FloatCurve_AddStop(&s_surfaceParticleRingBrake, 0.0f, 1.0f);
        FloatCurve_AddStop(&s_surfaceParticleRingBrake, 0.24f, 0.10f);
        FloatCurve_AddStop(&s_surfaceParticleRingBrake, 1.0f, 0.03f);
        s_surfaceParticleRingCurvesReady = true;
    }
    if (s_surfaceParticleRingReady[index]) return;
    const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(s_surfaceParticleRingSurfaces[index]);
    if (profile != NULL && profile->body.id != 0) {
        static const float rateMul[SURFACE_PARTICLE_RING_ANIM_RATES] = {1.0f, 0.91f, 0.82f, 0.74f};
        s_surfaceParticleRingTex[index] = profile->body;
        s_surfaceParticleRingNormalTex[index] = profile->normalMap;
        for (int i = 0; i < SURFACE_PARTICLE_RING_ANIM_RATES; ++i)
            SpriteAnim_Init(&s_surfaceParticleRingAnim[index][i], profile->flipbookColumns,
                            profile->flipbookRows, profile->flipbookFrames,
                            ((float)profile->flipbookFrames / 0.98f) * rateMul[i], ANIM_ONCE);
    }
    s_surfaceParticleRingReady[index] = true;
}

const char *VFX_SurfaceParticleRingVariant_Name(VFX_SurfaceParticleRingVariant variant)
{
    static const char *const names[VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT] = {
        "DUST PUFF", "DARK SMOKE PUFF", "SMOKE WISP", "ENERGY WISP"
    };
    return names[SurfaceParticleRing_Index(variant)];
}

void VFX_ComposeSurfaceParticleRing(Vector3 pos, VC_MaterialId matId, float scale,
                                    float severity01, VFX_SurfaceParticleRingVariant variant)
{
    const int style = SurfaceParticleRing_Index(variant);
    const bool energy = variant == VFX_SURFACE_PARTICLE_RING_VARIANT_ENERGY_WISP;
    SurfaceParticleRing_Init(variant);
    if (s_surfaceParticleRingTex[style].id == 0 || scale <= 0.0f) return;
    severity01 = Clamp(severity01, 0.0f, 1.0f);
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    const int count = SURFACE_PARTICLE_RING_MIN_PARTICLES +
        (int)(severity01 * (SURFACE_PARTICLE_RING_MAX_PARTICLES - SURFACE_PARTICLE_RING_MIN_PARTICLES));
    // R1 starts close to the contact point. R2 is authored by the outward
    // physical impulse below, so the ring reads as an expanding blast rather
    // than appearing fully formed at its final radius.
    const float initialRingRadius = Math_Mix(0.10f, 0.38f, severity01) * scale;
    for (int i = 0; i < count; ++i) {
        const float angle = ((float)i + Random01() * 0.55f) / (float)count * 2.0f * PI;
        const float radialJitter = Math_Mix(0.78f, 1.08f, Random01());
        const float speed = Math_Mix(3.4f, 5.8f, Random01()) * scale;
        Color base;
        Color tint;
        if (energy) {
            Color glow = mat ? mat->glow : (Color){150, 220, 255, 255};
            base = (Color){(unsigned char)((glow.r * 7 + 240 * 3) / 10),
                           (unsigned char)((glow.g * 7 + 250 * 3) / 10),
                           (unsigned char)((glow.b * 7 + 255 * 3) / 10), 156};
            tint = (Color){base.r, base.g, base.b, 92};
        } else {
            base = (Color){210, 202, 186, 112};
            tint = (Color){(unsigned char)((base.r * 8 + mat->body.r * 2) / 10),
                           (unsigned char)((base.g * 8 + mat->body.g * 2) / 10),
                           (unsigned char)((base.b * 8 + mat->body.b * 2) / 10), 56};
        }
        ParticleConfig p = {
            .position = {pos.x + cosf(angle) * initialRingRadius * radialJitter,
                         pos.y + 0.08f * scale,
                         pos.z + sinf(angle) * initialRingRadius * radialJitter},
            .velocity = (Vector3){0},
            .radius = Math_Mix(0.32f, 0.55f, Random01()) * scale,
            .lifetime = energy ? Math_Mix(0.52f, 0.85f, Random01()) : Math_Mix(0.72f, 1.08f, Random01()),
            .colorStart = tint, .colorEnd = VC_WithAlpha(tint, 0),
            .radiusCurve = &s_surfaceParticleRingGrow, .alphaCurve = &s_surfaceParticleRingFade,
            .physics.dynamics = &s_surfaceParticleRingDynamics,
            .physics.initialImpulseNs = {cosf(angle) * speed,
                Math_Mix(0.015f, 0.055f, Random01()) * scale, sinf(angle) * speed},
            .render.texture = s_surfaceParticleRingTex[style],
            .render.blendMode = energy ? VFX_BLEND_ADDITIVE : VFX_BLEND_ALPHA,
            .spriteAnim = &s_surfaceParticleRingAnim[style][i % SURFACE_PARTICLE_RING_ANIM_RATES],
            .spriteAnimPhase = Random01() * 0.16f, .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.10f,
        };
        // Smoke/dust sheets are semantic packed data and require their paired normal
        // map + decoder; plasma wisps are an emissive display flipbook instead.
        if (!energy) {
            p.render.smokeSheet = 1;
            p.render.normalTex = s_surfaceParticleRingNormalTex[style];
        } else {
            p.render.unlit = 1;
        }
        SpawnParticle(p);
    }
}

// Compatibility wrapper while gameplay migrations still refer to the old
// gameplay-oriented name. New callers should use SurfaceParticleRing + variant.
void VFX_ComposeGroundDustRing(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    VFX_ComposeSurfaceParticleRing(pos, matId, scale, severity01,
                                   VFX_SURFACE_PARTICLE_RING_VARIANT_DUST);
}
