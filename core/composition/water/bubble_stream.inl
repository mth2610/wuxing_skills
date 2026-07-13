void VFX_ComposeBubbleStream(Vector3 pos, float radius, float time)
{
    WaterFx_InitShared();

    static ForceField s_bubbleFld = {0};
    if (s_bubbleFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_GRAVITY_DIR,
                                              .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                              .strength = 1.1f});
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_NOISE_CURL,
                                              .strength = 0.5f,
                                              .noiseScale = 4.0f,
                                              .noiseSpeed = 0.8f});
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_VISCOSITY,
                                              .strength = 1.8f}); 
    }

    static ParticleConfig s_popDrops;
    s_popDrops = (ParticleConfig){
        .velocity = (Vector3){0.0f, 0.08f, 0.0f},
        .radius = 0.005f,
        .lifetime = 0.22f,
        .gradient = &s_dropCapGrad};

    if (GetRandomValue(0, 100) < 55)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * 0.5f * sqrtf(Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.03f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, 0.15f + Random01() * 0.1f, 0},
            .radius = 0.007f + Random01() * 0.009f,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_bubbleGrad,
            .radiusCurve = &s_softInOut,
            .forceField = &s_bubbleFld,
            .onDeathEmit = &s_popDrops,
            .onDeathEmitCount = 3});
    }

    if (GetRandomValue(0, 100) < 12)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){(Random01() - 0.5f) * radius,
                                                 radius * (0.4f + 0.4f * Random01()),
                                                 (Random01() - 0.5f) * radius}),
                       VFX_Material(VC_MAT_WATER)->soft, radius * 1.4f, 0.2f, VFX_PRIORITY_LOW);

    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D rippleTex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 5.0f,
                          radius * 0.7f, radius * 0.9f,
                          rippleTex, 1.4f, ColorAlpha(ELEMENT_COLOR_WATER, 0.35f),
                          BLEND_ADDITIVE, 0.015f);
    }

    (void)time;
}
