void VFX_ComposeRicochetSpark(Vector3 pos, Vector3 dir, float scale)
{
    MetalFx_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){0.0f, 1.0f, 0.0f};

    Vector3 up = fabsf(n.y) > 0.9f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Vector3 side = Vector3Normalize(Vector3CrossProduct(n, up));
    Vector3 side2 = Vector3CrossProduct(n, side);

    static ForceField s_sparkFld = {0};
    if (s_sparkFld.layerCount == 0)
        ForceField_AddLayer(&s_sparkFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 4.0f});

    int sparkCount = 8 + GetRandomValue(0, 4);
    for (int i = 0; i < sparkCount; i++)
    {
        float spread = 0.6f * Random01();
        float ring = Random01() * 2.0f * PI;
        Vector3 d = Vector3Normalize(Vector3Add(n,
                        Vector3Add(Vector3Scale(side, cosf(ring) * spread),
                                   Vector3Scale(side2, sinf(ring) * spread))));
        SpawnParticle((ParticleConfig){
            .position = pos,
            .velocity = Vector3Scale(d, (1.8f + Random01() * 1.5f) * scale),
            .radius = (0.007f + Random01() * 0.006f) * scale,
            .lifetime = 0.12f + Random01() * 0.15f,
            .gradient = &s_steelHotGrad,
            .forceField = &s_sparkFld});
    }

    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = WHITE,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.05f * scale,
        .lifetime = 0.05f});

    VFXLight_Spawn(pos, VFX_Material(VC_MAT_METAL)->soft, 0.9f * scale, 0.1f, VFX_PRIORITY_LOW);
}
