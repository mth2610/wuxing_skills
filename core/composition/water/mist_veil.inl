void VFX_ComposeMistVeil(Vector3 pos, float radius, float time)
{
    WaterFx_InitShared();

    static ForceField s_mistFld;
    ForceField_Clear(&s_mistFld);
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_VORTEX,
                                        .origin = pos,
                                        .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                        .strength = 0.5f,
                                        .radius = radius * 2.0f,
                                        .falloff = 1.0f});
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_NOISE_CURL,
                                        .strength = 0.35f,
                                        .noiseScale = 1.8f,
                                        .noiseSpeed = 0.5f});
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_VISCOSITY,
                                        .strength = 2.2f});

    if (GetRandomValue(0, 100) < 65)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * (0.3f + 0.7f * Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr,
                                  pos.y + 0.05f + Random01() * radius * 0.3f,
                                  pos.z + sinf(a) * rr},
            .velocity = (Vector3){-sinf(a) * 0.1f, 0.02f, cosf(a) * 0.1f},
            .radius = (0.06f + Random01() * 0.06f) * (radius / 1.0f),
            .lifetime = 1.4f + Random01() * 1.0f,
            .gradient = &s_mistGrad,
            .radiusCurve = &s_mistShape,
            .forceField = &s_mistFld});
    }

    if (GetRandomValue(0, 100) < 10)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01() * 0.8f;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + radius * 0.35f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, -0.1f, 0},
            .radius = 0.006f + Random01() * 0.004f,
            .lifetime = 0.5f + Random01() * 0.3f,
            .gradient = &s_dropGrad,
            .radiusCurve = &s_softInOut});
    }

    if (GetRandomValue(0, 100) < 8)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.25f, 0}),
                       VFX_Material(VC_MAT_WATER)->soft, radius * 1.6f,
                       0.3f, VFX_PRIORITY_LOW);

    (void)time;
}
