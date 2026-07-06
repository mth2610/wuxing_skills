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

// --- Wood ambience set -------------------------------------------------------
// Leaves/petals/spores are NOT geometry (same philosophy as fire in
// vc_fire.inl): a leaf is a small glowing packet whose FLUTTER — size
// pulsing + curl-noise sway — is what sells "foliage", since the particle
// system only has one round texture. Three pieces:
//   LeafSwirl  — continuous vortex of leaves around a point (buff/channel)
//   BloomBurst — one-shot blossom pop (heal proc, buff apply)
//   LeafFall   — continuous canopy of leaves fluttering down (zone/blessing)

static ColorGradient s_leafGrad = {0};      // body: vibrant green → dark fade
static ColorGradient s_leafHeroGrad = {0};  // standouts: sunlit near-white green
static ColorGradient s_pollenGrad = {0};    // pollen/spore: warm yellow-green glow
static SkillCurve s_leafFlutter = {0};      // size pulse: the "tumbling leaf" read
static SkillCurve s_leafFadeInOut = {0};    // no popping at birth/death
static bool s_woodAmbInit = false;

static void WoodAmbience_InitShared(void)
{
    if (s_woodAmbInit)
        return;

    ColorGradient_AddStop(&s_leafGrad, 0.0f, (Color){70, 220, 120, 230});
    ColorGradient_AddStop(&s_leafGrad, 0.6f, (Color){46, 204, 113, 180});
    ColorGradient_AddStop(&s_leafGrad, 1.0f, (Color){20, 90, 55, 0});

    ColorGradient_AddStop(&s_leafHeroGrad, 0.0f, (Color){200, 255, 210, 255});
    ColorGradient_AddStop(&s_leafHeroGrad, 0.5f, (Color){120, 240, 150, 210});
    ColorGradient_AddStop(&s_leafHeroGrad, 1.0f, (Color){40, 140, 80, 0});

    ColorGradient_AddStop(&s_pollenGrad, 0.0f, (Color){230, 255, 150, 0});
    ColorGradient_AddStop(&s_pollenGrad, 0.25f, (Color){210, 250, 130, 200});
    ColorGradient_AddStop(&s_pollenGrad, 1.0f, (Color){120, 180, 60, 0});

    // Flutter: irregular size beats — a leaf catching/losing the light as it
    // tumbles. Doubles as the emissive flicker curve.
    FloatCurve_AddStop(&s_leafFlutter, 0.0f, 0.7f);
    FloatCurve_AddStop(&s_leafFlutter, 0.2f, 1.25f);
    FloatCurve_AddStop(&s_leafFlutter, 0.45f, 0.75f);
    FloatCurve_AddStop(&s_leafFlutter, 0.7f, 1.15f);
    FloatCurve_AddStop(&s_leafFlutter, 1.0f, 0.6f);

    FloatCurve_AddStop(&s_leafFadeInOut, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_leafFadeInOut, 0.15f, 1.0f);
    FloatCurve_AddStop(&s_leafFadeInOut, 0.8f, 0.9f);
    FloatCurve_AddStop(&s_leafFadeInOut, 1.0f, 0.0f);

    s_woodAmbInit = true;
}

void VFX_ComposeLeafSwirl(Vector3 pos, float radius, float time)
{
    WoodAmbience_InitShared();

    // Vortex + updraft + curl, rebuilt each call so the origin tracks `pos`
    // (same single-static pattern as ChargeGetPullField).
    static ForceField s_swirlFld;
    ForceField_Clear(&s_swirlFld);
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_VORTEX,
                                         .origin = pos,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 3.0f,
                                         .radius = radius * 2.5f,
                                         .falloff = 1.0f});
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 0.5f});
    ForceField_AddLayer(&s_swirlFld, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = 0.8f,
                                         .noiseScale = 2.5f,
                                         .noiseSpeed = 1.2f});

    // Leaves enter at the base ring, get taken by the vortex, spiral up and
    // out. ~2/frame keeps a standing population of ~35.
    for (int i = 0; i < 2; i++)
    {
        if (GetRandomValue(0, 100) >= 80)
            continue;
        float a = Random01() * 2.0f * PI;
        float rr = radius * (0.55f + 0.45f * Random01());
        Vector3 spawn = {pos.x + cosf(a) * rr, pos.y + Random01() * radius * 0.25f,
                         pos.z + sinf(a) * rr};
        // Tangential launch so the vortex picks them up smoothly, no popping
        // from rest.
        Vector3 tangent = {-sinf(a), 0.15f + Random01() * 0.2f, cosf(a)};
        bool hero = GetRandomValue(0, 100) < 15;
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = Vector3Scale(tangent, 0.4f + Random01() * 0.3f),
            .radius = (hero ? 0.020f : 0.014f) * (0.8f + Random01() * 0.45f),
            .lifetime = 0.9f + Random01() * 0.7f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_swirlFld});
    }

    // Pollen shimmer drifting off the swirl — smaller, slower, warmer.
    if (GetRandomValue(0, 100) < 30)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.4f,
                                  pos.y + Random01() * radius * 0.6f,
                                  pos.z + sinf(a) * radius * 0.4f},
            .velocity = (Vector3){0, 0.12f + Random01() * 0.1f, 0},
            .radius = 0.006f + Random01() * 0.005f,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_swirlFld});
    }

    // Living ground — soft moss glow pooling under the swirl, gated with a
    // matching lifetime so per-frame callers keep one decal alive.
    if (GetRandomValue(0, 100) < 3)
    {
        Texture2D mossTex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 6.0f,
                          radius * 1.1f, radius * 1.35f,
                          mossTex, 1.2f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.35f),
                          BLEND_ADDITIVE, 0.02f);
    }

    // Green life-light breathing with the swirl.
    if (GetRandomValue(0, 100) < 20)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.4f, 0}),
                       ELEMENT_COLOR_WOOD, radius * 2.2f, 0.18f, VFX_PRIORITY_LOW);
}

void VFX_ComposeBloomBurst(Vector3 pos, float scale)
{
    WoodAmbience_InitShared();

    // Petals arc outward then fall — light downward pull + curl flutter.
    static ForceField s_petalFld = {0};
    if (s_petalFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 1.2f});
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_NOISE_CURL,
                                             .strength = 0.7f,
                                             .noiseScale = 3.0f,
                                             .noiseSpeed = 1.5f});
        ForceField_AddLayer(&s_petalFld, (ForceLayer){
                                             .type = FORCE_VISCOSITY,
                                             .strength = 1.5f});
    }

    // ① Petal ring — ejected outward at ~35° up, arcing over and fluttering
    // down. Per-petal randomization (12.3): angle jitter, speed, size, tier.
    int petalCount = 18 + GetRandomValue(0, 6);
    for (int i = 0; i < petalCount; i++)
    {
        float a = ((float)i / (float)petalCount) * 2.0f * PI + Random01() * 0.35f;
        float pitch = (30.0f + Random01() * 15.0f) * DEG2RAD;
        Vector3 dir = {cosf(a) * cosf(pitch), sinf(pitch), sinf(a) * cosf(pitch)};
        bool hero = GetRandomValue(0, 100) < 20;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.05f * scale)),
            .velocity = Vector3Scale(dir, (0.9f + Random01() * 0.6f) * scale),
            .radius = (hero ? 0.022f : 0.015f) * scale * (0.8f + Random01() * 0.4f),
            .lifetime = 0.7f + Random01() * 0.5f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_petalFld});
    }

    // ② Pollen puff — slow golden-green motes lifting out of the center,
    // outliving the petals: the afterglow of the bloom.
    for (int i = 0; i < 8; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.06f * scale,
                                  pos.y + 0.04f * scale,
                                  pos.z + sinf(a) * 0.06f * scale},
            .velocity = (Vector3){cosf(a) * 0.1f, 0.25f + Random01() * 0.2f, sinf(a) * 0.1f},
            .radius = (0.007f + Random01() * 0.006f) * scale,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_petalFld});
    }

    // ③ Center pop — small white-green flash so the bloom has a birth moment
    // (StreakFlare is white/hot — this stays soft and vegetal).
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = (Color){210, 255, 220, 240},
        .colorEnd = VC_WithAlpha(VFX_Material(VC_MAT_WOOD)->body, 0),
        .radius = 0.14f * scale,
        .lifetime = 0.16f});

    // ④ Ground blessing mark — root ring stamped under the bloom.
    Texture2D rootTex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
    DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 10.0f,
                      scale * 0.35f, scale * 0.8f,
                      rootTex, 1.0f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f),
                      BLEND_ADDITIVE, 0.03f);

    VFXLight_Spawn(pos, ELEMENT_COLOR_WOOD, scale * 2.0f, 0.35f, VFX_PRIORITY_LOW);
}

void VFX_ComposeLeafFall(Vector3 pos, float radius, float time)
{
    WoodAmbience_InitShared();

    // Gentle fall + strong lateral curl = the seesaw flutter of a real
    // falling leaf (dead-straight fall is the giveaway of cheap rain).
    static ForceField s_fallFld = {0};
    if (s_fallFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_GRAVITY_DIR,
                                            .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                            .strength = 0.9f});
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_NOISE_CURL,
                                            .strength = 1.4f,
                                            .noiseScale = 2.2f,
                                            .noiseSpeed = 1.0f});
        ForceField_AddLayer(&s_fallFld, (ForceLayer){
                                            .type = FORCE_VISCOSITY,
                                            .strength = 2.0f}); // terminal velocity — leaves never plummet
    }

    float canopyH = radius * 1.6f;

    // ~1-2 leaves/frame across the zone. Lifetime sized to reach the ground
    // so leaves visibly land and expire, not vanish mid-air.
    for (int i = 0; i < 2; i++)
    {
        if (GetRandomValue(0, 100) >= 65)
            continue;
        float a = Random01() * 2.0f * PI;
        float rr = radius * sqrtf(Random01());
        bool hero = GetRandomValue(0, 100) < 12;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr,
                                  pos.y + canopyH * (0.8f + 0.2f * Random01()),
                                  pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, -0.15f, (Random01() - 0.5f) * 0.2f},
            .radius = (hero ? 0.018f : 0.013f) * (0.8f + Random01() * 0.4f),
            .lifetime = 2.2f + Random01() * 1.2f,
            .gradient = hero ? &s_leafHeroGrad : &s_leafGrad,
            .radiusCurve = &s_leafFlutter,
            .emissiveCurve = &s_leafFlutter,
            .forceField = &s_fallFld});
    }

    // Drifting spore shimmer filling the air column, very sparse.
    if (GetRandomValue(0, 100) < 15)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01();
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + Random01() * canopyH, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, 0.05f + Random01() * 0.06f, 0},
            .radius = 0.005f + Random01() * 0.004f,
            .lifetime = 1.2f + Random01() * 0.8f,
            .gradient = &s_pollenGrad,
            .radiusCurve = &s_leafFadeInOut,
            .forceField = &s_fallFld});
    }

    // Mossy ground glow where the leaves gather.
    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D mossTex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 4.0f,
                          radius * 0.9f, radius * 1.05f,
                          mossTex, 1.5f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.28f),
                          BLEND_ADDITIVE, 0.015f);
    }

    // Dappled canopy light — dimmer and slower than LeafSwirl's.
    if (GetRandomValue(0, 100) < 12)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, canopyH * 0.5f, 0}),
                       VFX_Material(VC_MAT_WOOD)->soft, radius * 1.8f, 0.25f, VFX_PRIORITY_LOW);

    (void)time;
}
