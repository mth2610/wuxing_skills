void VFX_ComposeGlowingVine(Vector3 startPos, Vector3 targetPos, Vector3 p1, Vector3 p2, Vector3 contactPos, float progress, float time, float sizeScale, int branchIndex, int branchCount)
{
    const int pointCount = 128;
    static RibbonPoint ribbonPoints[128];

    float drawProgress = progress;
    if (drawProgress > 1.8f)
        drawProgress = 1.8f;

    for (int i = 0; i < pointCount; i++)
    {
        float norm = (float)i / (float)(pointCount - 1);
        float t = norm * drawProgress;

        Vector3 pt = GetWoodPointAt(startPos, p1, p2, contactPos, targetPos, t, sizeScale, branchIndex, branchCount);
        float taper = powf(1.0f - norm, 0.6f);

        ribbonPoints[i].position = pt;
        ribbonPoints[i].halfWidth = 0.03f * sizeScale * taper;
        ribbonPoints[i].v = norm;

        float alpha = 1.0f;
        if (progress > 1.8f)
        {
            alpha = 1.0f - (progress - 1.8f) / (2.4f - 1.8f);
            if (alpha < 0.0f)
                alpha = 0.0f;
        }
        ribbonPoints[i].tint = ColorAlpha(ELEMENT_COLOR_WOOD, alpha * 0.8f);
    }

    EffectMaterialParams matParams = {0};
    matParams.baseColor = ColorAlpha(ELEMENT_COLOR_WOOD, 0.8f);
    matParams.rimStrength = 2.2f;
    matParams.fresnelPower = 2.5f;
    matParams.emissiveIntensity = 1.8f;
    matParams.distortionStrength = 0.0f;
    matParams.translucency = 0.4f;

    EffectMaterial mat = Material_LoadCustom(matParams);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    Material_Begin(mat);
    DrawRibbonStrip(ribbonPoints, pointCount, (Texture2D){0}, camera);
    Material_End();

    BeginBlendMode(BLEND_ADDITIVE);
    matParams.emissiveIntensity = 3.0f;
    matParams.translucency = 0.0f;
    EffectMaterial matGlow = Material_LoadCustom(matParams);

    Material_Begin(matGlow);
    for (int i = 0; i < pointCount; i++)
    {
        ribbonPoints[i].halfWidth *= 0.45f;
        float alpha = progress > 1.8f ? (1.0f - (progress - 1.8f) / (2.4f - 1.8f)) : 1.0f;
        ribbonPoints[i].tint = ColorAlpha(WHITE, alpha * 0.35f);
    }
    DrawRibbonStrip(ribbonPoints, pointCount, (Texture2D){0}, camera);
    Material_End();

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
