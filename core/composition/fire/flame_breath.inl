void VFX_ComposeFlameBreath(Vector3 pos, Vector3 dir, float scale, float time)
{
    FireFlow_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 up = fabsf(n.y) > 0.9f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Vector3 side = Vector3Normalize(Vector3CrossProduct(n, up));
    Vector3 side2 = Vector3CrossProduct(n, side);

    float surge = 0.7f + 0.3f * sinf(time * 9.0f) * sinf(time * 5.3f + 1.7f);

    int jetCount = 2 + (int)(2.0f * surge);
    for (int i = 0; i < jetCount; i++)
    {
        float spread = 0.25f * Random01();
        float ring = Random01() * 2.0f * PI;
        Vector3 d = Vector3Normalize(Vector3Add(n,
                                                Vector3Add(Vector3Scale(side, cosf(ring) * spread),
                                                           Vector3Scale(side2, sinf(ring) * spread))));
        bool hot = GetRandomValue(0, 100) < 35; 
        if (hot)
            d = Vector3Normalize(Vector3Add(n, Vector3Scale(d, 0.15f)));
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(d, 0.06f * scale)),
            .velocity = Vector3Scale(d, (2.6f + Random01() * 1.2f) * scale * surge),
            .radius = (hot ? 0.030f : 0.048f) * scale * (0.8f + Random01() * 0.45f),
            .lifetime = 0.30f + Random01() * 0.18f,
            .gradient = hot ? &s_fireCoreGrad : &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    if (GetRandomValue(0, 100) < 25)
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(n, (0.8f + Random01() * 0.4f) * scale)),
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, 0.3f + Random01() * 0.2f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.05f * scale,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    if (GetRandomValue(0, 100) < 10)
        ScreenDistort_Add(Vector3Add(pos, Vector3Scale(n, 0.6f * scale)),
                          0.5f * scale, 0.1f, 0.4f, 2.0f);

    if (GetRandomValue(0, 100) < 30)
        VFXLight_Spawn(Vector3Add(pos, Vector3Scale(n, 0.2f * scale)),
                       VFX_Material(VC_MAT_FIRE)->soft, (1.4f + 0.6f * surge) * scale,
                       0.12f, VFX_PRIORITY_LOW);
}
