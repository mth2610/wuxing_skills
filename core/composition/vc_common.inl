#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>

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

// --- Beauty primitives -----------------------------------------------------
// Reusable "polish" pieces any skill/element can reach for directly, or that
// other composition functions build on. All LDR-safe — pure particle/decal/
// light work, no post-process pipeline involved (see CORE_ISSUES.md Item 35
// for what NOT to do here: shared bloom/streak tuning is fragile across GPUs;
// concentrating "sparkle" into small, short-lived, high-contrast primitives
// like these is the safe way to read as premium).

void VFX_ComposeShockwaveRing(Vector3 pos, float radius, float life, Color tint)
{
    static Texture2D s_ringTex = {0};
    static Texture2D s_flashTex = {0};
    if (s_ringTex.id == 0)
        s_ringTex = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png");
    if (s_flashTex.id == 0)
        s_flashTex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");

    // Randomized start rotation so repeated casts don't look identical (12.3).
    float rot = (float)GetRandomValue(0, 360);

    // ① Core ring — the crisp additive wavefront, brightest layer. Expands
    // 0.4x -> 1.8x radius over `life` with a slow spin so it never reads as
    // a static stamped circle.
    DecalSystem_AddEx(pos, rot, 22.0f,
                      radius * 0.4f, radius * 1.8f,
                      s_ringTex, life, tint, BLEND_ADDITIVE, 0.035f);

    // ② Outer soft glow — ~20% larger than the core ring, very low alpha.
    // Halos the wavefront so the bright line doesn't sit on a hard edge.
    DecalSystem_AddEx(pos, rot, 22.0f,
                      radius * 0.48f, radius * 2.15f,
                      s_flashTex, life * 1.1f, ColorAlpha(tint, 0.18f), BLEND_ADDITIVE, 0.02f);

    // Trailing echo ring — lags the wavefront, dimmer, counter-spins.
    // Gives the wave depth instead of a single line.
    DecalSystem_AddEx(pos, rot, -13.0f,
                      radius * 0.25f, radius * 1.55f,
                      s_ringTex, life * 1.3f, ColorAlpha(tint, 0.4f), BLEND_ADDITIVE, 0.025f);

    // ③ Distortion ring — the ground looks compressed by the pressure wave.
    // Peaks around mid-life (ScreenDistort's own envelope), strongest layer
    // for the "physical shock" read even though it adds no color.
    ScreenDistort_Add(pos, radius * 1.4f, 0.35f, life * 0.8f, 2.5f);

    // ④ Ground dust ripple — thin alpha-blended dust sheet chasing the ring,
    // desaturated so it reads as displaced dirt, not energy (not a particle).
    Color dust = {(unsigned char)(tint.r / 3 + 90), (unsigned char)(tint.g / 3 + 85),
                  (unsigned char)(tint.b / 3 + 80), 110};
    DecalSystem_AddEx(pos, rot + 45.0f, 8.0f,
                      radius * 0.55f, radius * 2.0f,
                      s_flashTex, life * 1.5f, dust, BLEND_ALPHA, 0.015f);

    // Flash — instant hot pop at the impact point, dies almost immediately.
    DecalSystem_AddEx(pos, rot, 0.0f,
                      radius * 0.12f, radius * 0.95f,
                      s_flashTex, life * 0.28f, ColorAlpha(WHITE, 0.85f), BLEND_ADDITIVE, 0.04f);

    // Light punch synced to the flash.
    VFXLight_Spawn(pos, tint, radius * 1.5f, life * 0.3f, VFX_PRIORITY_LOW);
}

void VFX_ComposeGlintBurst(Vector3 pos, int count, float spread, Color tint)
{
    // Faint gravity so sparks arc and fall away like struck-metal sparks
    // instead of flying dead-straight forever.
    static ForceField s_glintFld = {0};
    if (s_glintFld.layerCount == 0)
        ForceField_AddLayer(&s_glintFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 1.4f});

    // Sharp fade: sparks flare hot then die fast (short mid-point, strong
    // brighten) so each reads as a hot pinpoint, not a soft dot.
    static ColorGradient s_glintGrad = {0};
    ColorGradient_StandardFade(&s_glintGrad, tint, 0.12f, 1.1f);

    // A near-white "hero" gradient for a few standout sparks — keeps the
    // burst from being a uniform speckle of one colour.
    static ColorGradient s_hotGrad = {0};
    Color hot = {(unsigned char)(tint.r + (255 - tint.r) * 0.6f),
                 (unsigned char)(tint.g + (255 - tint.g) * 0.6f),
                 (unsigned char)(tint.b + (255 - tint.b) * 0.6f), 255};
    ColorGradient_StandardFade(&s_hotGrad, hot, 0.18f, 1.2f);

    // Tail emitter for long sparks — a fast head particle dropping a fading
    // dust line behind it is what actually reads as a "streak" (the shared
    // round particle texture can't stretch, so the tail supplies the length).
    static ParticleConfig s_longTail;
    s_longTail = (ParticleConfig){
        .radius = 0.007f,
        .lifetime = 0.1f,
        .gradient = &s_glintGrad};

    for (int i = 0; i < count; i++)
    {
        // 12.3 Instance Randomization — every glint gets its own direction,
        // distance, speed, size and lifetime so a burst never looks like a
        // stamped-out ring of identical sparks.
        float yaw = ((float)GetRandomValue(0, 3600)) / 10.0f * DEG2RAD;
        float pitch = ((float)GetRandomValue(-900, 900)) / 10.0f * DEG2RAD;
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        float dist = spread * (0.3f + 0.7f * Random01());
        Vector3 spawnPos = Vector3Add(pos, Vector3Scale(dir, dist));

        // Three spark tiers: ~5% LONG (fast, bright, streaking tail),
        // ~15% DOT (near-static bright pinpoints), rest SHORT (the body of
        // the burst). The mix is what sells "expensive" — uniform sparks
        // read as one cheap emitter.
        int roll = GetRandomValue(0, 99);
        if (roll < 5)
        {
            // Long spark — very fast, brighter, lives longer, drags a tail.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = Vector3Scale(dir, 2.2f + Random01() * 1.2f),
                .radius = 0.014f + Random01() * 0.006f,
                .lifetime = 0.2f + Random01() * 0.08f,
                .gradient = &s_hotGrad,
                .forceField = &s_glintFld,
                .onLiveEmit = &s_longTail,
                .onLiveEmitRate = 90.0f});
        }
        else if (roll < 20)
        {
            // Dot spark — barely moves, just a hot pinpoint blinking out.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = Vector3Scale(dir, 0.05f + Random01() * 0.1f),
                .radius = 0.008f + Random01() * 0.005f,
                .lifetime = 0.1f + Random01() * 0.08f,
                .gradient = &s_hotGrad});
        }
        else
        {
            // Short spark — the bread-and-butter scatter.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = Vector3Scale(dir, 0.5f + Random01() * 0.8f),
                .radius = 0.010f + Random01() * 0.008f,
                .lifetime = 0.12f + Random01() * 0.1f,
                .gradient = &s_glintGrad,
                .forceField = &s_glintFld});
        }
    }

    // Tiny lens-flare pop at the centre — 0.06s white micro-flash that makes
    // the whole burst feel like it came from one hot point of contact.
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = WHITE,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.05f + spread * 0.15f,
        .lifetime = 0.06f});
}

void VFX_ComposeEmberDrift(Vector3 pos, float radius, int count, Color tint)
{
    static ForceField s_emberFld = {0};
    if (s_emberFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_emberFld, (ForceLayer){
                                             .type = FORCE_NOISE_CURL,
                                             .strength = 0.3f,
                                             .noiseScale = 0.4f,
                                             .noiseSpeed = 0.6f});
        ForceField_AddLayer(&s_emberFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                             .strength = 0.15f});
    }

    static ColorGradient s_emberGrad = {0};
    ColorGradient_StandardFade(&s_emberGrad, tint, 0.3f, 0.6f);

    // Fade-in / hold / fade-out size so embers never pop in or snap out, and
    // an irregular emissive flicker for the "live coal" shimmer. Built once.
    static SkillCurve s_emberSize = {0};
    static SkillCurve s_emberFlicker = {0};
    static bool s_emberCurvesInit = false;
    if (!s_emberCurvesInit)
    {
        FloatCurve_AddStop(&s_emberSize, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_emberSize, 0.18f, 1.0f);
        FloatCurve_AddStop(&s_emberSize, 0.7f, 0.85f);
        FloatCurve_AddStop(&s_emberSize, 1.0f, 0.0f);
        FloatCurve_AddStop(&s_emberFlicker, 0.0f, 0.7f);
        FloatCurve_AddStop(&s_emberFlicker, 0.3f, 1.35f);
        FloatCurve_AddStop(&s_emberFlicker, 0.55f, 0.8f);
        FloatCurve_AddStop(&s_emberFlicker, 0.8f, 1.2f);
        FloatCurve_AddStop(&s_emberFlicker, 1.0f, 0.6f);
        s_emberCurvesInit = true;
    }

    // Dim, desaturated dust tint for the background layer.
    static ColorGradient s_dustGrad = {0};
    Color dustTint = {(unsigned char)(tint.r / 2 + 50), (unsigned char)(tint.g / 2 + 48),
                      (unsigned char)(tint.b / 2 + 45), 120};
    ColorGradient_StandardFade(&s_dustGrad, dustTint, 0.35f, 0.2f);

    // Near-white hot tint for the standout sparks.
    static ColorGradient s_sparkGrad = {0};
    Color sparkTint = {(unsigned char)(tint.r + (255 - tint.r) * 0.55f),
                       (unsigned char)(tint.g + (255 - tint.g) * 0.55f),
                       (unsigned char)(tint.b + (255 - tint.b) * 0.55f), 255};
    ColorGradient_StandardFade(&s_sparkGrad, sparkTint, 0.25f, 1.1f);

    for (int i = 0; i < count; i++)
    {
        float a = Random01() * 2.0f * PI;
        float r = radius * Random01();
        Vector3 spawnPos = {pos.x + cosf(a) * r,
                            pos.y + Random01() * radius * 0.3f,
                            pos.z + sinf(a) * r};

        // Slight horizontal drift seeds the curl-noise into a lazy sway
        // rather than a dead-vertical rise.
        float driftX = (Random01() - 0.5f) * 0.06f;
        float driftZ = (Random01() - 0.5f) * 0.06f;

        // Three tiers at different rise speeds — depth parallax inside the
        // cluster: slow faint dust behind, embers as the body, a few hot
        // sparks racing ahead. Uniform particles read as a single cheap layer.
        int roll = GetRandomValue(0, 99);
        if (roll < 30)
        {
            // Layer 1 — tiny dust motes: slowest (≈0.1 m/s), dim, smallest.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = (Vector3){driftX, 0.06f + Random01() * 0.08f, driftZ},
                .radius = 0.008f + Random01() * 0.006f,
                .lifetime = 1.6f + Random01() * 1.2f,
                .gradient = &s_dustGrad,
                .radiusCurve = &s_emberSize,
                .forceField = &s_emberFld});
        }
        else if (roll < 88)
        {
            // Layer 2 — embers: the main body (≈0.2–0.35 m/s), flickering.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = (Vector3){driftX, 0.2f + Random01() * 0.15f, driftZ},
                .radius = 0.018f + Random01() * 0.012f,
                .lifetime = 1.2f + Random01() * 1.0f, // long, lazy drift — ambient, not punchy
                .gradient = &s_emberGrad,
                .radiusCurve = &s_emberSize,
                .emissiveCurve = &s_emberFlicker,
                .forceField = &s_emberFld});
        }
        else
        {
            // Layer 3 — hot sparks: fastest (≈0.5 m/s), brightest, rare.
            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = (Vector3){driftX * 2.0f, 0.45f + Random01() * 0.2f, driftZ * 2.0f},
                .radius = 0.012f + Random01() * 0.008f,
                .lifetime = 0.9f + Random01() * 0.6f,
                .gradient = &s_sparkGrad,
                .radiusCurve = &s_emberSize,
                .emissiveCurve = &s_emberFlicker,
                .forceField = &s_emberFld});
        }
    }

    // Layer 4 — heat shimmer: a faint, slow screen-space wobble over the
    // column of rising air. Gated so per-frame ambient callers don't stack
    // distortions (the effect itself lives ~1s).
    if (GetRandomValue(0, 100) < 4)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, radius * 0.4f, 0}),
                          radius * 0.9f, 0.06f, 1.0f, 0.8f);
}

void VFX_ComposeStreakFlare(Vector3 pos, float scale, Color tint)
{
    // A bright flash-pop + light. The "premium flash" read comes from being
    // tiny, fast and high-contrast (the particle system shares one global
    // round texture — see core/particle_system.h — so shape comes from
    // size/decay, NOT a 4-point star sprite). Two stacked layers: a blinding
    // white core inside a tinted halo, both driven by a punch-out size curve
    // (snap up, collapse) so it never lingers as a soft blob.
    static SkillCurve s_popCurve = {0};
    static bool s_popInit = false;
    if (!s_popInit)
    {
        FloatCurve_AddStop(&s_popCurve, 0.0f, 0.25f);
        FloatCurve_AddStop(&s_popCurve, 0.25f, 1.0f);
        FloatCurve_AddStop(&s_popCurve, 1.0f, 0.0f);
        s_popInit = true;
    }

    VFXLight_Spawn(pos, tint, 2.0f * scale, 0.16f, VFX_PRIORITY_LOW);

    // Hot white core — the actual "flash". Very short (0.08s): the eye
    // catches the pop but never sees it linger.
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = WHITE,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.17f * scale,
        .lifetime = 0.08f,
        .radiusCurve = &s_popCurve});

    // Outer bloom — tinted, larger, and outliving the core (0.18s). The
    // core dying while the bloom still hangs is what reads as cinematic
    // afterglow instead of a simple blink.
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = tint,
        .colorEnd = (Color){tint.r, tint.g, tint.b, 0},
        .radius = 0.36f * scale,
        .lifetime = 0.18f,
        .radiusCurve = &s_popCurve});

    // Soft residual haze — very faint, biggest, last to go. Cushions the
    // bloom edge so the flare never ends on a hard circle.
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = ColorAlpha(tint, 0.25f),
        .colorEnd = (Color){tint.r, tint.g, tint.b, 0},
        .radius = 0.55f * scale,
        .lifetime = 0.24f,
        .radiusCurve = &s_popCurve});

    // Micro shock ripple — a barely-there air compression pop around the
    // flash point (tiny + short so it stays a garnish, not a shockwave).
    ScreenDistort_Add(pos, 0.5f * scale, 0.12f, 0.15f, 3.0f);
}
