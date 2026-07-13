void VFX_ComposeShrapnelBurst(Vector3 pos, float scale)
{
    MetalFx_InitShared();

    static ForceField s_fragFld = {0};
    if (s_fragFld.layerCount == 0)
        ForceField_AddLayer(&s_fragFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 7.0f});

    static ParticleConfig s_fragTail;
    s_fragTail = (ParticleConfig){
        .radius = 0.006f * scale,
        .lifetime = 0.12f,
        .gradient = &s_steelGrad};

    int fragCount = 16 + GetRandomValue(0, 8);
    for (int i = 0; i < fragCount; i++)
    {
        float yaw = Random01() * 2.0f * PI;
        float pitch = (Random01() * 50.0f - 5.0f) * DEG2RAD; 
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        int roll = GetRandomValue(0, 99);
        if (roll < 25)
        {
            SpawnParticle((ParticleConfig){ 
                .position = pos,
                .velocity = Vector3Scale(dir, (2.8f + Random01() * 1.4f) * scale),
                .radius = 0.013f * scale,
                .lifetime = 0.35f + Random01() * 0.2f,
                .gradient = &s_steelHotGrad,
                .forceField = &s_fragFld,
                .onLiveEmit = &s_fragTail,
                .onLiveEmitRate = 100.0f});
        }
        else
        {
            SpawnParticle((ParticleConfig){ 
                .position = pos,
                .velocity = Vector3Scale(dir, (1.2f + Random01() * 1.2f) * scale),
                .radius = (0.008f + Random01() * 0.007f) * scale,
                .lifetime = 0.3f + Random01() * 0.25f,
                .gradient = roll < 60 ? &s_steelGrad : &s_steelHotGrad,
                .forceField = &s_fragFld});
        }
    }

    VFX_ComposeStreakFlare(pos, 0.9f * scale, VFX_Material(VC_MAT_METAL)->soft);
    VFX_ComposeGlintBurst(pos, 10, 0.25f * scale, VFX_Material(VC_MAT_METAL)->soft);

    if (pos.y < 0.6f * scale)
    {
        Texture2D craterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
        DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, (float)GetRandomValue(0, 360),
                        0.55f * scale, craterTex, 2.5f, ColorAlpha(ELEMENT_COLOR_METAL, 0.8f));
    }

    ScreenDistort_Add(pos, 0.6f * scale, 0.22f, 0.25f, 2.5f);
    VFXLight_Spawn(pos, VFX_Material(VC_MAT_METAL)->soft, 2.2f * scale, 0.2f, VFX_PRIORITY_LOW);
}
