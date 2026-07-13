void VFX_ComposeLeafSwirl(Vector3 pos, float radius, float time)
{
    WoodAmbience_InitShared();

    static ForceField s_swirlFld;
    ForceField_Clear(&s_swirlFld);
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_VORTEX,
                                         .origin = pos,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 3.0f,
                                         .radius = radius * 2.5f,
                                         .falloff = 1.0f});
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 0.5f});
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = 0.8f,
                                         .noiseScale = 2.5f,
                                         .noiseSpeed = 1.2f});

    for (int i = 0; i < 2; i++)
    {
        if (GetRandomValue(0, 100) >= 80)
            continue;
        float a = Random01() * 2.0f * PI;
        float rr = radius * (0.55f + 0.45f * Random01());
        Vector3 spawn = {pos.x + cosf(a) * rr, pos.y + Random01() * radius * 0.25f,
                         pos.z + sinf(a) * rr};
        Vector3 tangent = {-sinf(a), 0.15f + Random01() * 0.2f, cosf(a)};
        bool hero = GetRandomValue(0, 100) < 15;
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = Vector3Scale(tangent, 0.4f + Random01() * 0.3f),
            .radius = (hero ? 0.020f : 0.014f) * (0.8f + Random01() * 0.45f),
            .lifetime = 0.9f + Random01() * 0.7f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_swirlFld});
    }

    if (GetRandomValue(0, 100) < 30)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.4f,
                                  pos.y + Random01() * radius * 0.6f,
                                  pos.z + sinf(a) * radius * 0.4f},
            .velocity = (Vector3){0, 0.12f + Random01() * 0.1f, 0},
            .radius = 0.006f + Random01() * 0.005f,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_swirlFld});
    }

    if (GetRandomValue(0, 100) < 3)
    {
        Texture2D mossTex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 6.0f,
                          radius * 1.1f, radius * 1.35f,
                          mossTex, 1.2f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.35f),
                          BLEND_ADDITIVE, 0.02f);
    }

    if (GetRandomValue(0, 100) < 20)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.4f, 0}),
                       ELEMENT_COLOR_WOOD, radius * 2.2f, 0.18f, VFX_PRIORITY_LOW);
}
