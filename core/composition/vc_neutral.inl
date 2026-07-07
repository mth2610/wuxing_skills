// Generic neutral VFX — effects with no elemental identity.
// SmokePuff, SmokeTrail: generic smoke/impact residue.
// LightningBolt: neutral proc-ray bolt (element-colored bolts live in vc_metal.inl).

void VFX_ComposeSmokePuff(Vector3 pos, float size)
{
    // size in meters; ~0.3-1.0m = torso-width smoke reference.
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin    = 20;
    cfg.countMax    = 35;
    cfg.speedMin    = 0.3f;
    cfg.speedMax    = 0.7f;
    cfg.radiusMin   = size * 0.2f;
    cfg.radiusMax   = size * 0.6f;
    cfg.lifetimeMin = 0.8f;
    cfg.lifetimeMax = 1.4f;
    cfg.pitchRange  = PI * 0.5f;
    cfg.upwardBias  = 0.5f;
    cfg.colorStart  = (Color){110, 100, 90, 190};
    cfg.colorEnd    = (Color){ 45,  45, 45,   0};

    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceField_AddLayer(&f, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 4.5f});
        ForceField_AddLayer(&f, (ForceLayer){
            .type = FORCE_GRAVITY_DIR, .direction = {0.0f, 1.0f, 0.0f}, .strength = 0.6f});
    }
    cfg.forceField = &f;

    // radiusMin/Max are already derived from `size`; pass sizeScale=1.0 to
    // avoid a second multiplication inside SpawnRadialBurst.
    ParticleSystem_SpawnRadialBurst(pos, 1.0f, &cfg);

    VFXLight_Spawn(pos, (Color){130, 120, 110, 255}, size * 0.8f, 0.12f, VFX_PRIORITY_LOW);
}

void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration)
{
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f) return;
    dir = Vector3Scale(dir, 1.0f / len);

    int numPuffs = (int)(len / 0.5f) + 1;

    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin    = 1;
    cfg.countMax    = 3;
    cfg.speedMin    = 0.1f;
    cfg.speedMax    = 0.25f;
    cfg.radiusMin   = 0.15f;
    cfg.radiusMax   = 0.4f;
    cfg.lifetimeMin = duration * 0.7f;
    cfg.lifetimeMax = duration * 1.3f;
    cfg.pitchRange  = PI;
    cfg.upwardBias  = 0.15f;
    cfg.colorStart  = (Color){110, 110, 110, 150};
    cfg.colorEnd    = (Color){ 45,  45,  45,   0};

    static ForceField f = {0};
    if (f.layerCount == 0)
        ForceField_AddLayer(&f, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 3.5f});
    cfg.forceField = &f;

    for (int i = 0; i <= numPuffs; i++) {
        float t   = (float)i / (float)numPuffs;
        Vector3 p = Vector3Add(start, Vector3Scale(dir, t * len));

        Vector3 perp = {-dir.z, 0.0f, dir.x};
        if (Vector3Length(perp) < 0.001f) perp = (Vector3){0.0f, 0.0f, 1.0f};
        perp = Vector3Normalize(perp);

        float jitter = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * 0.1f;
        p = Vector3Add(p, Vector3Scale(perp, jitter));

        ParticleSystem_SpawnRadialBurst(p, 1.0f, &cfg);
    }
}

int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale)
{
    int id = SpawnProcBolt(ProcRay_BoltLightningConfig(), scale);
    (void)start; // TODO: pass start when SpawnProcBolt gains a start/end overload

    VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 2.5f * scale, 0.25f, VFX_PRIORITY_HIGH_ULTIMATE);
    return id;
}
