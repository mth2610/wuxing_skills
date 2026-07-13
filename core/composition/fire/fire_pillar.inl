void VFX_ComposeFirePillar(Vector3 basePos, float progress)
{
    if (progress <= 0.0f)
        return;

    FireFlow_InitShared();

    float height = 1.6f;
    float baseRadius = 0.3f;
    float t = (float)GetTime();

    float rise = progress * progress * (3.0f - 2.0f * progress);
    float liveHeight = height * rise;

    float gust = 0.75f + 0.25f * sinf(t * 3.1f + basePos.x * 2.0f) * sinf(t * 1.7f + basePos.z);

    int bodyCount = 3 + (int)(3.0f * gust * rise);
    for (int i = 0; i < bodyCount; i++)
    {
        float armA = t * 5.5f + (float)i * (2.0f * PI / (float)bodyCount);
        float r = baseRadius * (0.35f + 0.65f * Random01());
        Vector3 spawn = {basePos.x + cosf(armA) * r, basePos.y, basePos.z + sinf(armA) * r};

        float life = 0.35f + Random01() * 0.3f;
        float upSpeed = liveHeight / fmaxf(life, 0.05f) * (0.8f + Random01() * 0.35f);
        Vector3 vel = {-sinf(armA) * 0.45f - cosf(armA) * r / life * 0.8f,
                       upSpeed,
                       cosf(armA) * 0.45f - sinf(armA) * r / life * 0.8f};

        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = vel,
            .radius = 0.07f * (0.75f + Random01() * 0.5f),
            .lifetime = life,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    if (GetRandomValue(0, 100) < (int)(85 * gust))
        FireFlow_EmitPacket(basePos, baseRadius * 0.18f, liveHeight * 1.1f, 0.6f, true, 1.6f);

    if (GetRandomValue(0, 100) < (int)(60 * gust))
    {
        float a = Random01() * 2.0f * PI;
        Vector3 spawn = {basePos.x + cosf(a) * baseRadius, basePos.y + 0.02f,
                         basePos.z + sinf(a) * baseRadius};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = (Vector3){cosf(a) * 0.25f, 0.35f + Random01() * 0.2f, sinf(a) * 0.25f},
            .radius = 0.06f + Random01() * 0.03f,
            .lifetime = 0.25f + Random01() * 0.15f,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    if (rise > 0.4f && GetRandomValue(0, 100) < (int)(30 * gust))
        SpawnParticle((ParticleConfig){
            .position = (Vector3){basePos.x + (Random01() - 0.5f) * baseRadius * 0.8f,
                                  basePos.y + liveHeight * (0.75f + Random01() * 0.3f),
                                  basePos.z + (Random01() - 0.5f) * baseRadius * 0.8f},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.15f, 0.35f + Random01() * 0.25f, (Random01() - 0.5f) * 0.15f},
            .radius = 0.07f + Random01() * 0.05f,
            .lifetime = 0.9f + Random01() * 0.7f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    if (GetRandomValue(0, 100) < (int)(25 * gust))
    {
        float a = Random01() * 2.0f * PI;
        Vector3 emberPos = {basePos.x + cosf(a) * baseRadius * 0.5f,
                            basePos.y + Random01() * liveHeight * 0.6f,
                            basePos.z + sinf(a) * baseRadius * 0.5f};
        VFX_ComposeEmberDrift(emberPos, baseRadius * 0.4f, 1, VFX_Material(VC_MAT_FIRE)->soft);
    }

    if (GetRandomValue(0, 100) < 6)
    {
        Texture2D glowTex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
        DecalSystem_AddEx(basePos, (float)GetRandomValue(0, 360), 15.0f,
                          baseRadius * 1.6f, baseRadius * 2.4f,
                          glowTex, 0.6f, VC_WithAlpha(VFX_Material(VC_MAT_FIRE)->soft, 140), BLEND_ADDITIVE, 0.02f);
    }

    if (GetRandomValue(0, 100) < 5)
        ScreenDistort_Add(Vector3Add(basePos, (Vector3){0, liveHeight + 0.15f, 0}),
                          baseRadius * 1.8f, 0.08f, 0.8f, 1.2f);

    if (GetRandomValue(0, 100) < 20)
        VFXLight_Spawn(Vector3Add(basePos, (Vector3){0, liveHeight * 0.3f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, baseRadius * (3.0f + 2.0f * gust) * rise,
                       0.2f, VFX_PRIORITY_LOW);
}
