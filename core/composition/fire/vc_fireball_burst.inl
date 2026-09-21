// NE_Explosion fireball primary — one instantaneous Niagara Fireball burst.
// This file is included after flame_volume.inl because that owner loads and
// validates the packed EOO Fireball sheet and its normal companion.

#define FIREBALL_BURST_MIN_PARTICLES 16
#define FIREBALL_BURST_MAX_PARTICLES 24
#define FIREBALL_BURST_EFFECTIVE_MASS_KG 1.25f

static const ParticleDynamicsProfile s_fireballBurstDynamics = {
    // Fire is a light gas volume: mass makes its authored N*s launch explicit,
    // while thermal acceleration and wind coupling describe its later motion.
    .inverseMassKg = 1.0f / FIREBALL_BURST_EFFECTIVE_MASS_KG,
    .gravityScale = 0.0f,
    .linearDragPerSecond = 3.5f,
    .terminalSpeedMps = 9.0f,
    .windCouplingHz = 1.1f,
    .windSusceptibility = 0.28f,
};

void VFX_ComposeFireballBurst(Vector3 pos, VC_MaterialId matId, float scale,
                              float severity01)
{
    if (scale <= 0.0f) return;
    FVol_InitShared();
    if (s_fvolFireballTex.id == 0) return;

    severity01 = Clamp(severity01, 0.0f, 1.0f);
    const int count = FIREBALL_BURST_MIN_PARTICLES +
        (int)(severity01 * (FIREBALL_BURST_MAX_PARTICLES - FIREBALL_BURST_MIN_PARTICLES) + 0.5f);
    const Texture2D ramp = FVol_RampLUT(matId);
    const ForceField *field = s_fvolFld.layerCount > 0 ? &s_fvolFld : NULL;
    const float impulseMass = FIREBALL_BURST_EFFECTIVE_MASS_KG;

    for (int i = 0; i < count; ++i)
    {
        // Uniform upper hemisphere: an expanding ball with thermal upward bias,
        // rather than a column of ambient flame sprites.
        Vector3 direction = VC_DirConeUniform((Vector3){0.0f, 1.0f, 0.0f},
                                               PI * 0.5f, Random01(), Random01());
        float startRadius = Math_Mix(0.10f, 0.40f, Random01()) * scale;
        Vector3 start = Vector3Add(pos, Vector3Scale(direction, startRadius));
        float launchSpeed = Math_Mix(4.0f, 8.0f, Random01()) * scale;
        Vector3 launchVelocity = Vector3Scale(direction, launchSpeed);
        launchVelocity.y += 2.0f * scale;

        SpawnParticle((ParticleConfig){
            .position = start,
            .velocity = (Vector3){0.0f, 0.0f, 0.0f},
            .radius = Math_Mix(0.60f, 0.95f, Random01()) * scale,
            .lifetime = Math_Mix(0.60f, 1.20f, Random01()),
            .colorStart = VC_WithAlpha(WHITE, 214),
            .colorEnd = (Color){48, 40, 36, 0},
            .forceField = field,
            .radiusCurve = &s_fvolGrow,
            .alphaCurve = &s_fvolFade,
            .emissiveCurve = &s_fvolCool,
            .physics.dynamics = &s_fireballBurstDynamics,
            .physics.initialImpulseNs = Vector3Scale(launchVelocity, impulseMass),
            .physics.initialAccelerationMps2 = {0.0f, 1.2f, 0.0f},
            .render.texture = s_fvolFireballTex,
            .render.normalTex = s_fvolFireballNormalTex,
            .render.volumeSheet = 4,
            .render.rampLUT = ramp,
            .render.heatGain = s_fvolHeatGain,
            .render.emissiveBoost = s_fvolEmissive,
            .render.blendMode = VFX_BLEND_PREMULTIPLIED,
            .render.unlit = 0,
            .spriteAnim = &s_fvolFireballAnim,
            .spriteAnimPhase = Random01() * FVOL_BODY_PHASE_MAX,
            .spriteAnimRate = Math_Mix(0.82f, 1.0f, Random01()),
            .spriteFlipX = Random01() < 0.5f,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 1.4f,
        });
    }

    const VFX_ElementMaterial *material = VFX_Material(matId);
    VFXLight_Spawn(pos, material->glow, 4.8f * scale, 0.18f, VFX_PRIORITY_LOW);
}
