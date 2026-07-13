// Preset-driven projectile trail — old-style preset API for flying projectiles.
// Uses VFX_ProjectilePreset from vfx_presets.h.
// (The material-id projectile lives in vc_projectile.inl.)

#define MAX_CONCURRENT_PROJECTILE_TRAILS 32
static ForceField s_flightFlds[MAX_CONCURRENT_PROJECTILE_TRAILS];
static int s_flightFldNextSlot = 0;

int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed)
{
    const VFX_ProjectilePreset *p = VFX_Preset_GetProjectile(preset);
    if (p == NULL) return -1;

    ForceField *flightFld = &s_flightFlds[s_flightFldNextSlot];
    s_flightFldNextSlot = (s_flightFldNextSlot + 1) % MAX_CONCURRENT_PROJECTILE_TRAILS;
    ForceField_Clear(flightFld);

    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));
    ForceField_AddLayer(flightFld, (ForceLayer){
        .type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 4.5f});
    ForceField_AddLayer(flightFld, (ForceLayer){
        .type = FORCE_NOISE_PERLIN, .strength = 0.5f, .noiseScale = 0.2f, .noiseSpeed = 3.0f});

    float travelTime = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.2f;

    static ParticleConfig s_tailEmit;
    s_tailEmit = (ParticleConfig){
        .radius     = 0.18f * scale,
        .lifetime   = 0.35f,
        .gradient   = p->gradient,
        .forceField = flightFld,
    };

    SpawnParticle((ParticleConfig){
        .position        = start,
        .velocity        = Vector3Scale(dir, speed),
        .colorStart      = p->tint,
        .colorEnd        = p->tint,
        .radius          = 0.25f * scale,
        .lifetime        = travelTime,
        .gradient        = p->gradient,
        .forceField      = flightFld,
        .onLiveEmit      = &s_tailEmit,
        .onLiveEmitRate  = 140.0f,
    });

    TrailConfig cfg = {
        .type        = TRAIL_TYPE_PROJECTILE,
        .pos         = start,
        .vel         = Vector3Scale(dir, speed),
        .len         = 0.15f * scale,
        .thick       = 0.25f * scale,
        .trailLength = 15.0f * scale,
        .life        = travelTime,
        .target      = target,
        .scale       = scale,
        .tint        = p->tint,
        .forceField  = flightFld,
        .gradient    = p->gradient,
        .priority    = VFX_PRIORITY_LOW,
    };
    return SpawnTrailEntity(cfg);
}
