void VFX_ComposeBladeRing(Vector3 pos, float radius, int bladeCount, float rotationDeg)
{
    static EffectMaterial s_bladeMat;
    static bool s_bladeMatLoaded = false;
    if (!s_bladeMatLoaded)
    {
        s_bladeMat = Material_Get(MAT_METAL);
        s_bladeMatLoaded = true;
    }

    float t = (float)GetTime();
    float breathe = 1.0f + 0.03f * sinf(t * 4.0f + pos.x);
    float wobble = 1.5f * sinf(t * 2.3f + pos.z);
    float liveRadius = radius * breathe;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(wobble, 1.0f, 0.0f, 0.0f); 
    rlRotatef(wobble * 0.7f, 0.0f, 0.0f, 1.0f);

    Material_Begin(s_bladeMat);
    rlPushMatrix();
    rlRotatef(rotationDeg, 0.0f, 1.0f, 0.0f);
    DrawCoreTorus((Vector3){0, 0, 0}, liveRadius * 0.55f, liveRadius * 0.78f, 6, 24, WHITE);
    rlPopMatrix();
    for (int i = 0; i < bladeCount; i++)
    {
        float a = ((float)i / (float)bladeCount) * 360.0f + rotationDeg;
        rlPushMatrix();
        rlRotatef(a, 0.0f, 1.0f, 0.0f);
        rlTranslatef(liveRadius * 0.78f, 0.0f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f); 
        rlRotatef(12.0f, 1.0f, 0.0f, 0.0f); 
        DrawCoreCone((Vector3){0, 0, 0}, 0.05f, liveRadius * 0.5f, 6, WHITE);
        rlPopMatrix();
    }
    Material_End();

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    EffectMaterialParams ghostParams = {0};
    ghostParams.baseColor = (Color){170, 215, 255, 70};
    ghostParams.emissiveIntensity = 1.2f;
    ghostParams.translucency = 0.3f;
    EffectMaterial ghostMat = Material_LoadCustom(ghostParams);
    Material_Begin(ghostMat);
    for (int i = 0; i < bladeCount; i++)
    {
        float a = ((float)i / (float)bladeCount) * 360.0f + rotationDeg - 9.0f; 
        rlPushMatrix();
        rlRotatef(a, 0.0f, 1.0f, 0.0f);
        rlTranslatef(liveRadius * 0.78f, 0.0f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        rlRotatef(12.0f, 1.0f, 0.0f, 0.0f);
        DrawCoreCone((Vector3){0, 0, 0}, 0.05f, liveRadius * 0.5f, 6, WHITE);
        rlPopMatrix();
    }
    Material_End();

    EffectMaterialParams edgeParams = {0};
    edgeParams.baseColor = (Color){200, 235, 255, 90};
    edgeParams.rimStrength = 2.0f;
    edgeParams.fresnelPower = 3.0f;
    edgeParams.emissiveIntensity = 1.4f;
    edgeParams.translucency = 0.6f;
    EffectMaterial edgeMat = Material_LoadCustom(edgeParams);
    Material_Begin(edgeMat);
    DrawCoreTorus((Vector3){0, 0, 0}, liveRadius * 1.02f, liveRadius * 1.1f, 5, 28, WHITE);
    Material_End();

    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){225, 240, 255, 255};
    coreParams.emissiveIntensity = 1.8f + 0.5f * sinf(t * 6.0f);
    EffectMaterial centerMat = Material_LoadCustom(coreParams);
    Material_Begin(centerMat);
    DrawCoreSphere((Vector3){0, 0, 0}, liveRadius * 0.12f, 12, 12, WHITE);
    Material_End();

    rlEnableDepthMask();
    EndBlendMode();

    rlPopMatrix();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();

    if (GetRandomValue(0, 100) < 12)
    {
        float ga = ((float)GetRandomValue(0, bladeCount - 1) / (float)bladeCount) * 360.0f + rotationDeg;
        Vector3 tip = {pos.x + cosf(ga * DEG2RAD) * liveRadius * 1.05f, pos.y,
                       pos.z - sinf(ga * DEG2RAD) * liveRadius * 1.05f};
        VFX_ComposeGlintBurst(tip, 2, 0.04f, VFX_Material(VC_MAT_METAL)->soft);
    }
}
