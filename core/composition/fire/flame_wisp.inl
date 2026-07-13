void VFX_ComposeFlameWisp(Vector3 pos, float time)
{
    FireFlow_InitShared();

    float phase = pos.x * 4.0f + pos.z * 2.7f;
    float flare = 0.7f + 0.3f * sinf(time * 6.0f + phase) * sinf(time * 4.1f + phase * 1.7f);
    Vector3 base = {pos.x + 0.015f * sinf(time * 3.0f + phase), pos.y, pos.z};

    FireFlow_EmitPacket(base, 0.045f, 0.16f + 0.06f * flare, 0.85f, false, 1.0f);
    if (GetRandomValue(0, 100) < 75)
        FireFlow_EmitPacket(base, 0.045f, 0.16f + 0.06f * flare, 0.85f, false, 1.0f);

    if (GetRandomValue(0, 100) < 55)
        FireFlow_EmitPacket(base, 0.015f, 0.19f + 0.07f * flare, 0.5f, true, 0.9f);

    if (GetRandomValue(0, 100) < 10)
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(base, (Vector3){0, 0.2f + 0.05f * flare, 0}),
            .velocity = (Vector3){(Random01() - 0.5f) * 0.05f, 0.2f + Random01() * 0.1f, (Random01() - 0.5f) * 0.05f},
            .radius = 0.03f,
            .lifetime = 0.7f + Random01() * 0.4f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    if (GetRandomValue(0, 100) < 5)
        VFX_ComposeEmberDrift(Vector3Add(base, (Vector3){0, 0.15f, 0}), 0.04f, 1,
                              VFX_Material(VC_MAT_FIRE)->soft);

    if (GetRandomValue(0, 100) < 25)
        VFXLight_Spawn(Vector3Add(base, (Vector3){0, 0.08f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, 0.55f + 0.2f * flare, 0.15f, VFX_PRIORITY_LOW);
}
