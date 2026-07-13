void VFX_ComposeLeafFall(Vector3 pos, float radius, float time)
{
    WoodAmbience_InitShared();

    static ForceField s_fallFld = {0};
    if (s_fallFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_GRAVITY_DIR,
                                            .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                            .strength = 0.9f});
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_NOISE_CURL,
                                            .strength = 1.4f,
                                            .noiseScale = 2.2f,
                                            .noiseSpeed = 1.0f});
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_VISCOSITY,
                                            .strength = 2.0f}); 
    }

    float canopyH = radius * 1.6f;

    for (int i = 0; i < 2; i++)
    {
        if (GetRandomValue(0, 100) >= 65)
            continue;
        float a = Random01() * 2.0f * PI;
        float rr = radius * sqrtf(Random01());
        bool hero = GetRandomValue(0, 100) < 12;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr,
                                  pos.y + canopyH * (0.8f + 0.2f * Random01()),
                                  pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, -0.15f, (Random01() - 0.5f) * 0.2f},
            .radius = (hero ? 0.018f : 0.013f) * (0.8f + Random01() * 0.4f),
            .lifetime = 2.2f + Random01() * 1.2f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_fallFld});
    }

    if (GetRandomValue(0, 100) < 15)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01();
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + Random01() * canopyH, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, 0.05f + Random01() * 0.06f, 0},
            .radius = 0.005f + Random01() * 0.004f,
            .lifetime = 1.2f + Random01() * 0.8f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_fallFld});
    }

    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D mossTex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 4.0f,
                          radius * 0.9f, radius * 1.05f,
                          mossTex, 1.5f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.28f),
                          BLEND_ADDITIVE, 0.015f);
    }

    if (GetRandomValue(0, 100) < 12)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, canopyH * 0.5f, 0}),
                       VFX_Material(VC_MAT_WOOD)->soft, radius * 1.8f, 0.25f, VFX_PRIORITY_LOW);

    (void)time;
}
