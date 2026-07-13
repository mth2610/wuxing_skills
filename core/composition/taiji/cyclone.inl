void VFX_ComposeCyclone(Vector3 pos, float radius, float time)
{
    TaijiFx_InitShared();

    float height = radius * 2.4f;

    static ForceField s_cycFld;
    ForceField_Clear(&s_cycFld);
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_VORTEX,
                                       .origin = pos,
                                       .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                       .strength = 6.0f,
                                       .radius = radius * 3.0f,
                                       .falloff = 1.0f});
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_GRAVITY_DIR,
                                       .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                       .strength = 2.2f});
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_GRAVITY_POINT,
                                       .origin = Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                                       .strength = 1.8f,
                                       .radius = radius * 3.0f,
                                       .falloff = 1.0f});

    int bodyCount = 3;
    for (int i = 0; i < bodyCount; i++)
    {
        float armA = time * 7.0f + (float)i * (2.0f * PI / (float)bodyCount);
        float rr = radius * (0.5f + 0.3f * Random01());
        Vector3 spawn = {pos.x + cosf(armA) * rr, pos.y + Random01() * height * 0.15f,
                         pos.z + sinf(armA) * rr};
        Vector3 tangent = {-sinf(armA), 0.3f, cosf(armA)};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = Vector3Scale(tangent, 1.2f),
            .radius = 0.014f + Random01() * 0.010f,
            .lifetime = 0.7f + Random01() * 0.5f,
            .gradient = &s_windGrad,
            .forceField = &s_cycFld});
    }

    if (GetRandomValue(0, 100) < 55)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.9f, pos.y + 0.03f,
                                  pos.z + sinf(a) * radius * 0.9f},
            .velocity = (Vector3){-sinf(a) * 0.8f, 0.1f, cosf(a) * 0.8f},
            .radius = 0.035f + Random01() * 0.03f,
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_windDustGrad,
            .forceField = &s_cycFld});
    }

    if (GetRandomValue(0, 100) < 12)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.4f,
                                  pos.y + Random01() * height * 0.5f,
                                  pos.z + sinf(a) * radius * 0.4f},
            .velocity = (Vector3){-sinf(a) * 1.5f, 0.8f, cosf(a) * 1.5f},
            .radius = 0.009f + Random01() * 0.006f,
            .lifetime = 0.8f + Random01() * 0.5f,
            .gradient = &s_windDustGrad,
            .forceField = &s_cycFld});
    }

    if (GetRandomValue(0, 100) < 8)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                          radius * 1.6f, 0.09f, 0.7f, 1.2f);
}
