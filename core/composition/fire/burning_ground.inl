void VFX_ComposeBurningGround(Vector3 pos, float radius, float time)
{
    FireFlow_InitShared();

    int tongues = 2 + GetRandomValue(0, 1);
    for (int i = 0; i < tongues; i++)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01() * Random01(); 
        Vector3 spot = {pos.x + cosf(a) * rr, pos.y, pos.z + sinf(a) * rr};
        FireFlow_EmitPacket(spot, 0.04f, 0.12f + 0.08f * Random01(), 0.8f,
                            GetRandomValue(0, 100) < 20, 0.9f);
    }

    if (GetRandomValue(0, 100) < 3)
    {
        Texture2D lavaTex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 3.0f,
                          radius * 0.95f, radius * 1.05f,
                          lavaTex, 1.3f, ColorAlpha(VFX_Material(VC_MAT_FIRE)->soft, 0.7f),
                          BLEND_ADDITIVE, 0.02f);
    }

    if (GetRandomValue(0, 100) < 15)
    {
        float a = Random01() * 2.0f * PI;
        VFX_ComposeEmberDrift((Vector3){pos.x + cosf(a) * radius * 0.5f, pos.y + 0.05f,
                                        pos.z + sinf(a) * radius * 0.5f},
                              radius * 0.3f, 1, VFX_Material(VC_MAT_FIRE)->soft);
    }
    if (GetRandomValue(0, 100) < 8)
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + (Random01() - 0.5f) * radius,
                                  pos.y + 0.25f,
                                  pos.z + (Random01() - 0.5f) * radius},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.1f, 0.3f + Random01() * 0.15f, (Random01() - 0.5f) * 0.1f},
            .radius = 0.045f + Random01() * 0.03f,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    if (GetRandomValue(0, 100) < 22)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.2f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, radius * (2.0f + 0.6f * Random01()),
                       0.15f, VFX_PRIORITY_LOW);
}
