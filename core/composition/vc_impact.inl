// Impact burst system — all entry points that build ImpactBurstConfig and fire the 4-step burst.
//
// Entry points:
//   VFX_ComposeTriggerImpactBurst — low-level, caller provides ImpactBurstConfig directly.
//   VFX_ComposeImpact             — preset API (EffectPresetType → VFX_ImpactPreset).
//   VFX_TriggerExplosion          — material API (VC_MaterialId → element material table).
//
// Scale convention (1 unit = 1 meter):
//   particles.speedMin/Max — direct m/s, no internal throttle factor.
//   lightRadius            — multiplied by sizeScale inside TriggerImpactBurst.
//   colorStart             — auto-resolved from gradient at t=0 if unset (alpha 0 = invisible).

void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg)
{
    if (cfg == NULL) return;

    if (cfg->distortEnabled)
        ScreenDistort_Add(pos, cfg->distortRadius * sizeScale, cfg->distortStrength,
                          cfg->distortLife, cfg->distortSpeed);

    if (cfg->decalEnabled) {
        float rot = cfg->decalRandomRotation
            ? (float)GetRandomValue(0, 360) : cfg->decalFixedRotation;
        DecalSystem_Add(pos, rot, cfg->decalScale * sizeScale,
                        cfg->decalTex, cfg->decalLife, cfg->decalTint);
    }

    if (cfg->lightEnabled)
        VFXLight_Spawn(pos, cfg->lightColor, cfg->lightRadius * sizeScale,
                       cfg->lightLife, VFX_PRIORITY_LOW);

    if (cfg->particlesEnabled) {
        const ParticleRadialBurstConfig *p = &cfg->particles;
        int count = GetRandomValue(p->countMin, p->countMax);
        count = (int)((float)count * sizeScale);
        if (count < 1) count = 1;

        // Resolve colorStart/End from gradient so colorStart.a is always valid.
        // Particles with alpha=0 are invisible even with a gradient set.
        Color startColor = p->gradient
            ? ColorGradient_Sample(p->gradient, 0.0f) : p->colorStart;
        Color endColor   = p->gradient
            ? ColorGradient_Sample(p->gradient, 1.0f) : p->colorEnd;

        for (int i = 0; i < count; i++) {
            float angle = Random01() * 2.0f * PI;
            float pitch = (Random01() - 0.5f) * p->pitchRange;
            float speed = Math_Mix(p->speedMin, p->speedMax, Random01());
            SpawnParticle((ParticleConfig){
                .position   = pos,
                .velocity   = {
                    cosf(angle) * cosf(pitch) * speed,
                    sinf(pitch) * speed + p->upwardBias,
                    sinf(angle) * cosf(pitch) * speed,
                },
                .radius     = Math_Mix(p->radiusMin, p->radiusMax, Random01()) * sizeScale,
                .lifetime   = Math_Mix(p->lifetimeMin, p->lifetimeMax, Random01()),
                .colorStart  = startColor,
                .colorEnd    = endColor,
                .gradient   = p->gradient,
            });
        }
    }
}

// --- Material-id entry point (VC_MaterialId → element material table) ---

void VFX_TriggerExplosion(VC_MaterialId matId, Vector3 pos, float scale, bool cameraShake)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    bool crackDecal = (matId == VC_MAT_ICE || matId == VC_MAT_LIGHTNING ||
                       matId == VC_MAT_EARTH || matId == VC_MAT_METAL);

    float lightR = 3.5f;
    if      (matId == VC_MAT_LIGHTNING)                          lightR = 4.0f;
    else if (matId == VC_MAT_HOLY)                               lightR = 4.5f;
    else if (matId == VC_MAT_VOID)                               lightR = 3.8f;
    else if (matId == VC_MAT_POISON)                             lightR = 3.2f;
    else if (matId == VC_MAT_ICE || matId == VC_MAT_EARTH)      lightR = 3.0f;

    ImpactBurstConfig cfg = {
        .distortEnabled = true,
        .distortRadius  = 2.0f * scale, .distortStrength = 0.3f,
        .distortLife    = 0.4f,         .distortSpeed    = 4.5f,

        .decalEnabled        = true,
        .decalTex            = ResourceManager_LoadTexture(crackDecal
            ? "assets/textures/decals/decal_crack.png"
            : "assets/textures/decals/decal_burn.png"),
        .decalScale          = 1.0f, .decalLife = 3.0f,
        .decalTint           = WHITE, .decalRandomRotation = true,

        .lightEnabled = true,
        .lightColor   = mat->glow, .lightRadius = lightR, .lightLife = 0.35f,

        .particlesEnabled = true,
        .particles = {
            .countMin = 25, .countMax = 40,
            .speedMin = 0.5f, .speedMax = 1.8f,
            .radiusMin = 0.1f, .radiusMax = 0.4f,
            .lifetimeMin = 0.4f, .lifetimeMax = 0.8f,
            .pitchRange = PI * 0.8f, .upwardBias = 0.3f,
            .gradient   = mat->grad,
            .forceField = (ForceField *)mat->fld,
        },
    };
    VFX_ComposeTriggerImpactBurst(pos, scale, &cfg);

    if (cameraShake) CameraFX_Shake(0.35f * scale);
}

// --- Preset-id entry point (EffectPresetType → VFX_ImpactPreset) ---

void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_ImpactPreset *p = VFX_Preset_GetImpact(preset);
    if (p == NULL) return;

    scale = Clamp(scale, 0.3f, 3.0f);
    PlayImpactSound(preset);
    if (scale >= 1.5f) TimeFX_Hitstop(0.06f, 0.04f);

    ImpactBurstConfig cfg = {0};
    cfg.distortEnabled  = p->distortEnabled;
    cfg.distortRadius   = p->distortRadius;
    cfg.distortStrength = p->distortStrength;
    cfg.distortLife     = p->distortLife;
    cfg.distortSpeed    = p->distortSpeed;

    cfg.decalEnabled        = p->decalEnabled;
    cfg.decalTex            = (p->decalPreset == DECAL_PRESET_BURN)
        ? ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png")
        : ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
    cfg.decalScale          = p->decalScale;
    cfg.decalLife           = p->decalLife;
    cfg.decalTint           = (p->decalTint.a > 0) ? p->decalTint : WHITE;
    cfg.decalRandomRotation = true;

    cfg.lightEnabled  = p->lightEnabled;
    cfg.lightColor    = p->lightColor;
    cfg.lightRadius   = p->lightRadius;
    cfg.lightLife     = p->lightLife;

    cfg.particlesEnabled = p->particlesEnabled;
    cfg.particles        = p->particles;

    VFX_ComposeTriggerImpactBurst(pos, scale, &cfg);
}
