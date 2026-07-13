void VFX_ComposeGustSlash(Vector3 pos, Vector3 dir, float scale)
{
    TaijiFx_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 side = Vector3Normalize((Vector3){-n.z, 0.0f, n.x});

    int streakCount = 10 + GetRandomValue(0, 4);
    for (int i = 0; i < streakCount; i++)
    {
        float f = ((float)i / (float)(streakCount - 1)) * 2.0f - 1.0f; 
        Vector3 d = Vector3Normalize(Vector3Add(n, Vector3Scale(side, f * 0.7f)));
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(side, f * 0.25f * scale)),
            .velocity = Vector3Scale(d, (3.0f + Random01() * 1.5f) * scale),
            .radius = (0.010f + Random01() * 0.008f) * scale,
            .lifetime = 0.18f + Random01() * 0.1f,
            .gradient = &s_windGrad});
    }

    for (int i = 0; i < 5; i++)
    {
        float f = Random01() * 2.0f - 1.0f;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(side, f * 0.3f * scale)),
            .velocity = Vector3Add(Vector3Scale(n, 0.6f), Vector3Scale(side, f * 0.9f)),
            .radius = (0.03f + Random01() * 0.025f) * scale,
            .lifetime = 0.4f + Random01() * 0.3f,
            .gradient = &s_windDustGrad});
    }

    Texture2D grooveTex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
    float yawDeg = atan2f(-n.z, n.x) * RAD2DEG;
    DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, yawDeg, 0.6f * scale,
                    grooveTex, 0.8f, ColorAlpha((Color){210, 235, 240, 255}, 0.55f));

    ScreenDistort_Add(Vector3Add(pos, Vector3Scale(n, 0.4f * scale)),
                      0.5f * scale, 0.15f, 0.2f, 3.0f);
}
