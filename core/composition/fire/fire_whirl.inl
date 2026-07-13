void VFX_ComposeFireWhirl(Vector3 pos, float radius, float time)
{
    FireFlow_InitShared();

    float height = radius * 3.0f;

    static ForceField s_whirlFld;
    ForceField_Clear(&s_whirlFld);
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_VORTEX,
                                         .origin = pos,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 7.0f,
                                         .radius = radius * 2.5f,
                                         .falloff = 1.0f});
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 3.0f});
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_POINT,
                                         .origin = Vector3Add(pos, (Vector3){0, height * 0.6f, 0}),
                                         .strength = 2.0f,
                                         .radius = radius * 3.0f,
                                         .falloff = 1.0f});

    int arms = 3;
    for (int i = 0; i < arms; i++)
    {
        float armA = time * 8.0f + (float)i * (2.0f * PI / (float)arms);
        float rr = radius * (0.45f + 0.35f * Random01());
        Vector3 spawn = {pos.x + cosf(armA) * rr, pos.y + Random01() * height * 0.1f,
                         pos.z + sinf(armA) * rr};
        Vector3 tangent = {-sinf(armA) * 1.0f, 0.5f, cosf(armA) * 1.0f};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = tangent,
            .radius = 0.05f * (0.75f + Random01() * 0.5f),
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_whirlFld});
    }

    if (GetRandomValue(0, 100) < 70)
        FireFlow_EmitPacket(pos, radius * 0.15f, height * 0.6f, 0.5f, true, 1.4f);

    if (GetRandomValue(0, 100) < 30)
    {
        float a = Random01() * 2.0f * PI;
        Vector3 rim = {pos.x + cosf(a) * radius, pos.y + Random01() * height * 0.6f,
                       pos.z + sinf(a) * radius};
        SpawnParticle((ParticleConfig){
            .position = rim,
            .velocity = (Vector3){-sinf(a) * 1.8f, 0.4f, cosf(a) * 1.8f},
            .radius = 0.010f + Random01() * 0.008f,
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_fireCoreGrad,
            .forceField = &s_flameFld}); 
    }

    if (GetRandomValue(0, 100) < 20)
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + (Random01() - 0.5f) * radius,
                                  pos.y + height * (0.8f + Random01() * 0.25f),
                                  pos.z + (Random01() - 0.5f) * radius},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.3f, 0.4f + Random01() * 0.3f, (Random01() - 0.5f) * 0.3f},
            .radius = 0.07f + Random01() * 0.05f,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    if (GetRandomValue(0, 100) < 12)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                          radius * 2.0f, 0.12f, 0.7f, 1.5f);
    if (GetRandomValue(0, 100) < 35)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, height * 0.4f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, radius * (4.0f + Random01()),
                       0.15f, VFX_PRIORITY_LOW);
}
