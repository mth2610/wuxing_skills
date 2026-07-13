void VFX_ComposeRockBurst(Vector3 pos, float scale)
{
    EarthFx_InitShared();

    int chunkCount = 14 + GetRandomValue(0, 6);
    for (int i = 0; i < chunkCount; i++)
    {
        float yaw = Random01() * 2.0f * PI;
        float pitch = (20.0f + Random01() * 50.0f) * DEG2RAD;
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        bool grain = GetRandomValue(0, 100) < 25;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.05f * scale)),
            .velocity = Vector3Scale(dir, (1.3f + Random01() * 1.5f) * scale),
            .radius = (grain ? 0.008f : 0.014f) * scale * (0.75f + Random01() * 0.5f),
            .lifetime = 0.5f + Random01() * 0.3f,
            .gradient = grain ? &s_earthGrainGrad : &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    for (int i = 0; i < 8; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.12f * scale, pos.y + 0.04f,
                                  pos.z + sinf(a) * 0.12f * scale},
            .velocity = (Vector3){cosf(a) * (0.4f + Random01() * 0.3f),
                                  0.1f + Random01() * 0.15f,
                                  sinf(a) * (0.4f + Random01() * 0.3f)},
            .radius = (0.05f + Random01() * 0.045f) * scale,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }

    Texture2D shatterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
    DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, (float)GetRandomValue(0, 360),
                    0.6f * scale, shatterTex, 3.5f, ColorAlpha(WHITE, 0.85f));

    CameraFX_Shake(0.25f * fminf(scale, 1.5f));
    ScreenDistort_Add(pos, 0.5f * scale, 0.15f, 0.2f, 1.2f);
    VFXLight_Spawn(pos, VFX_Material(VC_MAT_EARTH)->soft, 1.2f * scale, 0.15f, VFX_PRIORITY_LOW);
}
