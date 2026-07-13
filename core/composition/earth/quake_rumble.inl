void VFX_ComposeQuakeRumble(Vector3 pos, float radius, float time)
{
    EarthFx_InitShared();

    int hops = 1 + GetRandomValue(0, 1);
    for (int i = 0; i < hops; i++)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * sqrtf(Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.02f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.15f,
                                  0.6f + Random01() * 0.6f,
                                  (Random01() - 0.5f) * 0.15f},
            .radius = 0.006f + Random01() * 0.006f,
            .lifetime = 0.35f + Random01() * 0.15f,
            .gradient = GetRandomValue(0, 100) < 30 ? &s_earthGrainGrad : &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    if (GetRandomValue(0, 100) < 35)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01();
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.03f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, 0.15f + Random01() * 0.1f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.04f + Random01() * 0.03f,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }

    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D shatterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
        float a = Random01() * 2.0f * PI;
        float rr = radius * 0.7f * Random01();
        DecalSystem_Add((Vector3){pos.x + cosf(a) * rr, 0.0f, pos.z + sinf(a) * rr},
                        (float)GetRandomValue(0, 360), 0.35f + Random01() * 0.25f,
                        shatterTex, 2.5f, ColorAlpha(WHITE, 0.7f));
    }

    if (GetRandomValue(0, 100) < 25)
        CameraFX_Shake(0.05f);

    (void)time;
}
