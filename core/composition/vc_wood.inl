static Vector3 GetWoodPointAt(Vector3 startPos, Vector3 p1, Vector3 p2, Vector3 contactPos, Vector3 targetPos, float t, float sizeScale, int branchIndex, int branchCount)
{
    /* KHẮC PHỤC RẬP KHUÔN ĐỨNG YÊN:
     * Khuếch đại mạnh hệ số hash tọa độ. Các phép toán dưới đây đảm bảo chỉ cần nhân vật
     * hoặc mục tiêu nhích 0.001 unit, pha của dây leo sẽ xoay đi một góc hoàn toàn khác. */
    float castSeedPhase = (startPos.x * 137.3f + startPos.z * 313.1f + targetPos.x * 914.4f + targetPos.z * 519.9f);
    float branchPhase = (branchCount > 1) ? ((float)branchIndex / (float)branchCount) * 2.0f * PI : 0.0f;
    branchPhase += castSeedPhase;

    if (t <= 1.0f)
    {
        Vector3 pos = GetBezierPoint(startPos, p1, p2, contactPos, t);
        Vector3 dir = Vector3Normalize(Vector3Subtract(contactPos, startPos));
        Vector3 perp = (Vector3){-dir.z, 0, dir.x};
        if (Vector3Length(perp) == 0.0f)
            perp = (Vector3){0, 0, 1};
        perp = Vector3Normalize(perp);

        float envelope = sinf(t * PI);

        /* LÀM MƯỢT RỄ BÒ ĐẤT:
         * Giảm tần số sóng sin xuống thấp (5.0 và 9.0) để rễ uốn lượn chậm rãi mềm mại,
         * loại bỏ hiện tượng ziczac gắt như điện tâm đồ. */
        float wave1 = sinf(t * 5.0f + branchPhase);
        float wave2 = sinf(t * 9.0f + branchPhase * 1.618f) * 0.4f;
        float organicSway = (wave1 + wave2) * 0.4f * sizeScale * envelope;

        pos = Vector3Add(pos, Vector3Scale(perp, organicSway));

        float archHeight = 0.05f + 0.02f * (branchIndex % 3);
        pos.y += envelope * archHeight * sizeScale + 0.02f;

        return pos;
    }
    else
    {
        float t_wrap = t - 1.0f;
        float ratio = t_wrap / 0.8f;
        if (ratio > 1.0f)
            ratio = 1.0f;

        /* CHỐNG ĐỨT GÃY HÌNH (Normal Inversion):
         * Thay vì dùng hàm sinf gây lật trục ngẫu nhiên, ta dùng SmoothStep (Toán học C1 continuous)
         * để đạo hàm của vòng xoắn luôn tiến tới mượt mà, giúp lưới Ribbon không bị cuộn lật 180 độ. */
        float easeRatio = ratio * ratio * (3.0f - 2.0f * ratio);

        float sphereRadius = 1.25f * sizeScale;
        Vector3 sphereCenter = {targetPos.x, targetPos.y + sphereRadius, targetPos.z};

        float startPhi = PI * 0.95f;
        float endPhi = PI * 0.05f;
        float phi = startPhi - easeRatio * (startPhi - endPhi);

        float contactAngle = atan2f(contactPos.z - targetPos.z, contactPos.x - targetPos.x);

        float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;

        // Điều chỉnh lại số vòng xoắn cho vừa vặn mềm mại
        float baseTurns = (branchCount == 1) ? 3.5f : 1.8f;
        float turns = baseTurns + 0.4f * (branchIndex % 3);

        // Loại bỏ hoàn toàn High-Frequency Noise ở trục Theta gây gãy khúc
        float theta = easeRatio * turns * (2.0f * PI) * coilDir + contactAngle + branchPhase;

        // Độ gồ ghề bề mặt (Wobble) chỉ áp dụng lên bán kính với biên độ thấp
        float wobble = (sinf(theta * 2.0f) * 0.05f + cosf(phi * 3.0f) * 0.04f) * sizeScale;
        float currentRadius = sphereRadius + wobble;

        return (Vector3){
            sphereCenter.x + currentRadius * sinf(phi) * cosf(theta),
            sphereCenter.y + currentRadius * cosf(phi),
            sphereCenter.z + currentRadius * sinf(phi) * sinf(theta)};
    }
}

void VFX_ComposeGlowingVine(Vector3 startPos, Vector3 targetPos, Vector3 p1, Vector3 p2, Vector3 contactPos, float progress, float time, float sizeScale, int branchIndex, int branchCount)
{
    /* CHỐNG HÌNH ĐA GIÁC (Octagon Effect):
     * Nâng số điểm ảnh (Resolution) lên 128 điểm.
     * Chia quỹ lưới đủ dày cho 3.5 vòng xoắn để đường cong thực sự tròn trịa. */
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
