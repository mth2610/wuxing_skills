// Preset-driven compositions — high-level functions that drive the EffectPresetType /
// VFX_*Preset system. These are the "old-style" API (preset lookup → configure burst)
// kept separate from the material-id archetype layer (vc_shield, vc_zone, etc.).
//
// Also contains: generic neutral effects (SmokePuff, SmokeTrail, LightningBolt) that
// don't belong to any element but are too small for their own file.

#define MAX_CONCURRENT_CAST_EFFECTS 16
static ForceField s_castPullFlds[MAX_CONCURRENT_CAST_EFFECTS];
static int s_castPullFldNextSlot = 0;

#define MAX_CONCURRENT_PROJECTILE_TRAILS 32
static ForceField s_flightFlds[MAX_CONCURRENT_PROJECTILE_TRAILS];
static int s_flightFldNextSlot = 0;

static ForceField s_impactDampingFld;
static bool s_impactFldInited = false;

static void InitImpactForceField(void)
{
    if (s_impactFldInited)
        return;
    ForceField_Clear(&s_impactDampingFld);

    ForceLayer viscosity = {0};
    viscosity.type = FORCE_VISCOSITY;
    viscosity.strength = 8.0f;
    ForceField_AddLayer(&s_impactDampingFld, viscosity);

    ForceLayer noise = {0};
    noise.type = FORCE_NOISE_CURL;
    noise.strength = 0.5f;
    noise.noiseScale = 0.2f;
    noise.noiseSpeed = 1.5f;
    ForceField_AddLayer(&s_impactDampingFld, noise);

    s_impactFldInited = true;
}

void VFX_ComposeSmokePuff(Vector3 pos, float size)
{
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 20;
    cfg.countMax = 35;
    cfg.speedMin = 0.3f;
    cfg.speedMax = 0.7f;
    cfg.radiusMin = size * 0.2f;
    cfg.radiusMax = size * 0.6f;
    cfg.lifetimeMin = 0.8f;
    cfg.lifetimeMax = 1.4f;
    cfg.pitchRange = PI * 0.5f;
    cfg.upwardBias = 0.5f;

    cfg.colorStart = (Color){90, 85, 80, 180};
    cfg.colorEnd = (Color){40, 40, 40, 0};

    static ForceField f = {0};
    if (f.layerCount == 0)
    {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 4.5f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;

    ParticleSystem_SpawnRadialBurst(pos, size, &cfg);
}

void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration)
{
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f)
        return;
    dir = Vector3Scale(dir, 1.0f / len);

    int numPuffs = (int)(len / 0.5f) + 1;

    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 1;
    cfg.countMax = 3;
    cfg.speedMin = 0.1f;
    cfg.speedMax = 0.25f;
    cfg.radiusMin = 0.15f;
    cfg.radiusMax = 0.4f;
    cfg.lifetimeMin = duration * 0.7f;
    cfg.lifetimeMax = duration * 1.3f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 0.15f;

    cfg.colorStart = (Color){110, 110, 110, 150};
    cfg.colorEnd = (Color){45, 45, 45, 0};

    static ForceField f = {0};
    if (f.layerCount == 0)
    {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 3.5f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;

    for (int i = 0; i <= numPuffs; i++)
    {
        float t = (float)i / (float)numPuffs;
        Vector3 pos = Vector3Add(start, Vector3Scale(dir, t * len));

        Vector3 perp = {-dir.z, 0.0f, dir.x};
        if (Vector3Length(perp) < 0.001f)
            perp = (Vector3){0.0f, 0.0f, 1.0f};
        perp = Vector3Normalize(perp);
        float jitter = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * 0.1f;
        pos = Vector3Add(pos, Vector3Scale(perp, jitter));

        ParticleSystem_SpawnRadialBurst(pos, 1.0f, &cfg);
    }
}

int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale)
{
    int id = SpawnProcBolt(ProcRay_BoltLightningConfig(), scale);
    VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 2.5f * scale, 0.25f, VFX_PRIORITY_HIGH_ULTIMATE);
    return id;
    (void)start;
}

void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg)
{
    if (cfg == NULL)
        return;

    if (cfg->distortEnabled)
    {
        ScreenDistort_Add(pos, cfg->distortRadius, cfg->distortStrength,
                          cfg->distortLife, cfg->distortSpeed);
    }

    if (cfg->decalEnabled)
    {
        float rotation = cfg->decalRandomRotation
                             ? (float)GetRandomValue(0, 360)
                             : cfg->decalFixedRotation;
        DecalSystem_Add(pos, rotation, cfg->decalScale * sizeScale,
                        cfg->decalTex, cfg->decalLife, cfg->decalTint);
    }

    if (cfg->lightEnabled)
    {
        VFXLight_Spawn(pos, cfg->lightColor, cfg->lightRadius * sizeScale, cfg->lightLife, VFX_PRIORITY_LOW);
    }

    if (cfg->particlesEnabled)
    {
        ParticleRadialBurstConfig burstCfg = cfg->particles;
        burstCfg.speedMin = cfg->particles.speedMin * 0.3f * sizeScale;
        burstCfg.speedMax = cfg->particles.speedMax * 0.4f * sizeScale;
        burstCfg.radiusMin *= sizeScale;
        burstCfg.radiusMax *= sizeScale;

        InitImpactForceField();
        burstCfg.forceField = &s_impactDampingFld;

        ParticleSystem_SpawnRadialBurst(pos, sizeScale, &burstCfg);
    }
}

void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_ImpactPreset *p = VFX_Preset_GetImpact(preset);
    if (p == NULL)
        return;

    PlayImpactSound(preset);

    if (scale >= 1.5f)
        TimeFX_Hitstop(0.06f, 0.04f);

    InitImpactForceField();

    ImpactBurstConfig config = {0};
    config.distortEnabled = p->distortEnabled;
    config.distortRadius = p->distortRadius * scale;
    config.distortStrength = p->distortStrength;
    config.distortLife = p->distortLife;
    config.distortSpeed = p->distortSpeed;

    config.decalEnabled = p->decalEnabled;
    config.decalTex = p->decalPreset == 0 ? ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png") : ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
    config.decalScale = p->decalScale;
    config.decalLife = p->decalLife;
    config.decalTint = WHITE;
    config.decalRandomRotation = true;

    config.lightEnabled = p->lightEnabled;
    config.lightColor = p->lightColor;
    config.lightRadius = p->lightRadius;
    config.lightLife = p->lightLife;

    config.particlesEnabled = p->particlesEnabled;
    config.particles = p->particles;

    VFX_ComposeTriggerImpactBurst(pos, scale, &config);
}

void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_CastPreset *p = VFX_Preset_GetCast(preset);
    if (p == NULL)
        return;

    PlayCastSound(preset);

    VFXLight_Spawn(pos, p->flashColor, p->lightRadius * scale, p->lightLifetime, VFX_PRIORITY_LOW);

    ForceField *castPullFld = &s_castPullFlds[s_castPullFldNextSlot];
    s_castPullFldNextSlot = (s_castPullFldNextSlot + 1) % MAX_CONCURRENT_CAST_EFFECTS;
    ForceField_Clear(castPullFld);

    float spawnRadius = p->spawnRadius * scale;
    float pullStrength = p->pullStrength * scale;

    ForceField_AddLayer(castPullFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_POINT,
                                         .origin = pos,
                                         .strength = pullStrength * 1.5f,
                                         .radius = spawnRadius * 2.0f,
                                         .falloff = 1.0f});

    ForceField_AddLayer(castPullFld, (ForceLayer){
                                         .type = FORCE_VORTEX,
                                         .origin = pos,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = pullStrength * 0.8f,
                                         .radius = spawnRadius * 1.8f,
                                         .falloff = 1.0f});

    int count = (int)((float)p->particleCount * scale);
    for (int i = 0; i < count; i++)
    {
        float a = (float)i / count * 2.0f * PI + ((float)rand() / (float)RAND_MAX * 0.5f);
        float r = spawnRadius * (0.8f + 0.4f * ((float)rand() / (float)RAND_MAX));
        Vector3 spawnPos = {
            pos.x + cosf(a) * r,
            pos.y + ((float)rand() / (float)RAND_MAX) * spawnRadius * 0.4f,
            pos.z + sinf(a) * r};

        Vector3 toCenter = Vector3Subtract(pos, spawnPos);
        Vector3 tangent = Vector3Normalize((Vector3){-toCenter.z, 0.0f, toCenter.x});
        Vector3 initVel = Vector3Add(Vector3Scale(Vector3Normalize(toCenter), 0.25f), Vector3Scale(tangent, 0.1f));

        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = initVel,
            .radius = ((float)rand() / (float)RAND_MAX * 0.015f + 0.006f) * scale,
            .lifetime = (float)rand() / (float)RAND_MAX * 0.4f + 0.3f,
            .gradient = p->gradient,
            .forceField = castPullFld});
    }
}

int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed)
{
    const VFX_ProjectilePreset *p = VFX_Preset_GetProjectile(preset);
    if (p == NULL)
        return -1;

    ForceField *flightFld = &s_flightFlds[s_flightFldNextSlot];
    s_flightFldNextSlot = (s_flightFldNextSlot + 1) % MAX_CONCURRENT_PROJECTILE_TRAILS;
    ForceField_Clear(flightFld);
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));

    ForceField_AddLayer(flightFld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 4.5f});
    ForceField_AddLayer(flightFld, (ForceLayer){.type = FORCE_NOISE_PERLIN, .strength = 0.5f, .noiseScale = 0.2f, .noiseSpeed = 3.0f});

    static ParticleConfig s_tailEmit;
    s_tailEmit = (ParticleConfig){
        .radius = 0.18f * scale,
        .lifetime = 0.35f,
        .gradient = p->gradient,
        .forceField = flightFld};

    SpawnParticle((ParticleConfig){
        .position = start,
        .velocity = Vector3Scale(dir, speed),
        .colorStart = p->tint,
        .colorEnd = p->tint,
        .radius = 0.25f * scale,
        .lifetime = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.2f,
        .gradient = p->gradient,
        .forceField = flightFld,
        .onLiveEmit = &s_tailEmit,
        .onLiveEmitRate = 140.0f});

    TrailConfig cfg = {
        .type = TRAIL_TYPE_PROJECTILE,
        .pos = start,
        .vel = Vector3Scale(dir, speed),
        .len = 0.15f * scale,
        .thick = 0.25f * scale,
        .trailLength = 15.0f * scale,
        .life = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.2f,
        .target = target,
        .scale = scale,
        .tint = p->tint,
        .forceField = flightFld,
        .gradient = p->gradient,
        .priority = VFX_PRIORITY_LOW};
    return SpawnTrailEntity(cfg);
}
