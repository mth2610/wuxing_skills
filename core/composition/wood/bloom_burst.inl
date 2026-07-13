void VFX_ComposeBloomBurst(Vector3 pos, float scale)
{
    WoodAmbience_InitShared();

    static ForceField s_petalFld = {0};
    if (s_petalFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 1.2f});
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_NOISE_CURL,
                                             .strength = 0.7f,
                                             .noiseScale = 3.0f,
                                             .noiseSpeed = 1.5f});
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_VISCOSITY,
                                             .strength = 1.5f});
    }

    int petalCount = 18 + GetRandomValue(0, 6);
    for (int i = 0; i < petalCount; i++)
    {
        float a = ((float)i / (float)petalCount) * 2.0f * PI + Random01() * 0.35f;
        float pitch = (30.0f + Random01() * 15.0f) * DEG2RAD;
        Vector3 dir = {cosf(a) * cosf(pitch), sinf(pitch), sinf(a) * cosf(pitch)};
        bool hero = GetRandomValue(0, 100) < 20;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.05f * scale)),
            .velocity = Vector3Scale(dir, (0.9f + Random01() * 0.6f) * scale),
            .radius = (hero ? 0.022f : 0.015f) * scale * (0.8f + Random01() * 0.4f),
            .lifetime = 0.7f + Random01() * 0.5f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_petalFld});
    }

    for (int i = 0; i < 8; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.06f * scale,
                                  pos.y + 0.04f * scale,
                                  pos.z + sinf(a) * 0.06f * scale},
            .velocity = (Vector3){cosf(a) * 0.1f, 0.25f + Random01() * 0.2f, sinf(a) * 0.1f},
            .radius = (0.007f + Random01() * 0.006f) * scale,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_petalFld});
    }

    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = (Color){210, 255, 220, 240},
        .colorEnd = VC_WithAlpha(VFX_Material(VC_MAT_WOOD)->body, 0),
        .radius = 0.14f * scale,
        .lifetime = 0.16f});

    Texture2D rootTex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
    DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 10.0f,
                      scale * 0.35f, scale * 0.8f,
                      rootTex, 1.0f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f),
                      BLEND_ADDITIVE, 0.03f);

    VFXLight_Spawn(pos, ELEMENT_COLOR_WOOD, scale * 2.0f, 0.35f, VFX_PRIORITY_LOW);
}
