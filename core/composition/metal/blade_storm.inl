void VFX_ComposeBladeStorm(Vector3 pos, float radius, float time)
{
    MetalFx_InitShared();

    static EffectMaterial s_stormMat;
    static bool s_stormMatLoaded = false;
    if (!s_stormMatLoaded)
    {
        s_stormMat = Material_Get(MAT_METAL);
        s_stormMatLoaded = true;
    }

    rlDrawRenderBatchActive();
    Material_Begin(s_stormMat);
    const int bladeCount = 7;
    Vector3 tips[7];
    for (int i = 0; i < bladeCount; i++)
    {
        unsigned int h = (unsigned int)i * 2654435761u;
        float r01 = (float)(h >> 8 & 0xFFFF) / 65535.0f;
        float r02 = (float)(h >> 16 & 0xFFFF) / 65535.0f;

        int band = i % 2;
        float spin = (band == 0 ? 2.6f : -3.4f) * (0.85f + 0.3f * r01);
        float a = time * spin + (float)i * (2.0f * PI / (float)bladeCount);
        float orbR = radius * (0.75f + 0.35f * r01);
        float y = radius * (0.35f + 0.4f * r02) + radius * 0.12f * sinf(time * 2.3f + (float)i * 1.7f);

        Vector3 p = {pos.x + cosf(a) * orbR, pos.y + y, pos.z + sinf(a) * orbR};
        tips[i] = p;

        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(-a * RAD2DEG, 0, 1, 0);                 
        rlRotatef(90.0f * (band == 0 ? 1.0f : -1.0f), 1, 0, 0); 
        rlRotatef((r02 - 0.5f) * 40.0f, 0, 0, 1);         
        DrawCoreCone((Vector3){0, -radius * 0.11f, 0}, radius * 0.035f, radius * 0.22f, 5, WHITE);
        rlPopMatrix();
    }
    Material_End();
    rlDrawRenderBatchActive();

    if (GetRandomValue(0, 100) < 60)
    {
        int i = GetRandomValue(0, bladeCount - 1);
        Vector3 toCenter = Vector3Subtract(pos, tips[i]);
        Vector3 tangent = Vector3Normalize((Vector3){-toCenter.z, 0.0f, toCenter.x});
        if (i % 2 == 1)
            tangent = Vector3Scale(tangent, -1.0f);
        SpawnParticle((ParticleConfig){
            .position = tips[i],
            .velocity = Vector3Scale(tangent, 1.2f + Random01() * 0.6f),
            .radius = 0.010f + Random01() * 0.006f,
            .lifetime = 0.12f + Random01() * 0.08f,
            .gradient = &s_steelGrad});
    }

    if (GetRandomValue(0, 100) < 10)
        VFX_ComposeGlintBurst(tips[GetRandomValue(0, bladeCount - 1)], 2, 0.04f,
                              VFX_Material(VC_MAT_METAL)->soft);

    if (GetRandomValue(0, 100) < 15)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.5f, 0}),
                       VFX_Material(VC_MAT_METAL)->soft, radius * 2.0f, 0.15f, VFX_PRIORITY_LOW);
}
