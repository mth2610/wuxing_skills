void VFX_ComposeSplashBurst(Vector3 pos, float scale)
{
    WaterFx_InitShared();

    static ForceField s_dropFld = {0};
    if (s_dropFld.layerCount == 0)
        ForceField_AddLayer(&s_dropFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 9.8f});

    int dropCount = 16 + GetRandomValue(0, 6);
    for (int i = 0; i < dropCount; i++)
    {
        float a = ((float)i / (float)dropCount) * 2.0f * PI + Random01() * 0.3f;
        float pitch = (55.0f + Random01() * 20.0f) * DEG2RAD;
        Vector3 dir = {cosf(a) * cosf(pitch), sinf(pitch), sinf(a) * cosf(pitch)};
        bool cap = GetRandomValue(0, 100) < 20;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.04f * scale)),
            .velocity = Vector3Scale(dir, (1.4f + Random01() * 0.9f) * scale),
            .radius = (cap ? 0.016f : 0.011f) * scale * (0.8f + Random01() * 0.4f),
            .lifetime = 0.45f + Random01() * 0.25f,
            .gradient = cap ? &s_dropCapGrad : &s_dropGrad,
            .forceField = &s_dropFld});
    }

    for (int i = 0; i < 5; i++)
        SpawnParticle((ParticleConfig){
            .position = pos,
            .velocity = (Vector3){(Random01() - 0.5f) * 0.4f,
                                  (2.2f + Random01() * 0.8f) * scale,
                                  (Random01() - 0.5f) * 0.4f},
            .radius = 0.013f * scale,
            .lifetime = 0.5f + Random01() * 0.2f,
            .gradient = &s_dropCapGrad,
            .forceField = &s_dropFld});

    for (int i = 0; i < 6; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.1f * scale, pos.y + 0.05f,
                                  pos.z + sinf(a) * 0.1f * scale},
            .velocity = (Vector3){cosf(a) * 0.2f, 0.15f + Random01() * 0.1f, sinf(a) * 0.2f},
            .radius = (0.05f + Random01() * 0.04f) * scale,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_mistGrad,
            .radiusCurve = &s_mistShape});
    }

    Texture2D splashTex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
    Texture2D rippleTex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
    Vector3 ground = {pos.x, 0.0f, pos.z};
    DecalSystem_AddEx(ground, (float)GetRandomValue(0, 360), 15.0f,
                      scale * 0.15f, scale * 0.85f,
                      splashTex, 0.55f, ColorAlpha(VFX_Material(VC_MAT_WATER)->soft, 0.8f),
                      BLEND_ADDITIVE, 0.03f);
    DecalSystem_AddEx(ground, (float)GetRandomValue(0, 360), -8.0f,
                      scale * 0.1f, scale * 0.6f,
                      rippleTex, 0.9f, ColorAlpha(ELEMENT_COLOR_WATER, 0.5f),
                      BLEND_ADDITIVE, 0.02f);

    VFXLight_Spawn(pos, VFX_Material(VC_MAT_WATER)->soft, 1.6f * scale, 0.2f, VFX_PRIORITY_LOW);
    ScreenDistort_Add(pos, 0.4f * scale, 0.1f, 0.25f, 1.5f);
}
