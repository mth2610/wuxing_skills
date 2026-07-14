void VFX_ComposePlasmaOrb(Vector3 pos, float radius, float time)
{
    if (radius <= 0.0f)
        return;

    float dt = GetFrameTime();
    float breathe = 1.0f + 0.03f * sinf(time * 2.1f) + 0.015f * sinf(time * 5.3f);
    float r = radius * breathe;

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    EffectMaterialParams bloomParams = {0};
    bloomParams.baseColor = (Color){130, 215, 255, 70};
    bloomParams.emissiveIntensity = 0.8f + 0.15f * sinf(time * 5.0f);
    bloomParams.rimStrength = 0.4f;
    bloomParams.fresnelPower = 1.4f;
    bloomParams.translucency = 0.9f;
    EffectMaterial bloomMat;
    Material_LoadCustom(&bloomMat, &bloomParams);
    Material_Begin(bloomMat);
    DrawCoreSphere(pos, r * 0.30f, 12, 12, WHITE);
    Material_End();
    rlDrawRenderBatchActive();

    static ColorGradient s_wispTrailGrad = {0};
    static ForceField s_wispCurlFld = {0};
    static bool s_wispInit = false;
    if (!s_wispInit)
    {
        ColorGradient_AddStop(&s_wispTrailGrad, 0.0f, (Color){255, 255, 255, 255});
        ColorGradient_AddStop(&s_wispTrailGrad, 0.15f, (Color){255, 120, 165, 255});
        ColorGradient_AddStop(&s_wispTrailGrad, 0.7f, (Color){110, 45, 200, 150});
        ColorGradient_AddStop(&s_wispTrailGrad, 1.0f, (Color){50, 20, 150, 0});

        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL,
                                                .strength = 4.2f,
                                                .noiseScale = 15.5f,
                                                .noiseSpeed = 2.9f});

        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY,
                                                .strength = 15.6f});
        s_wispInit = true;
    }

    float trailSpawnRate = 100.0f;

    if (Random01() < (trailSpawnRate * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float z = 2.0f * Random01() - 1.0f;
        float r_xy = sqrtf(1.0f - z * z);
        Vector3 dir = (Vector3){r_xy * cosf(theta), z, r_xy * sinf(theta)};

        float innerBound = r * 0.35f;
        float outerBound = r * 0.75f;
        float startDist = innerBound + Random01() * (outerBound - innerBound);

        float life = 0.5f + Random01() * 0.9f;
        float driftSpeed = r * (0.12f + 0.10f * Random01());

        TrailConfig tCfg = {0};
        tCfg.type = TRAIL_TYPE_WISP;
        tCfg.pos = Vector3Add(pos, Vector3Scale(dir, startDist));
        tCfg.vel = Vector3Scale(dir, driftSpeed);

        tCfg.tint = WHITE;
        tCfg.blendMode = BLEND_ADDITIVE;

        tCfg.life = life;
        tCfg.thick = 0.008f * (radius / 0.5f);
        tCfg.len = r * 0.15f;

        tCfg.gradient = &s_wispTrailGrad;
        tCfg.forceField = &s_wispCurlFld;
        tCfg.priority = 0;

        SpawnTrailEntity(tCfg);
    }

    rlDisableBackfaceCulling();

    static PlasmaMaterial s_shellOuter, s_shellInner;
    static bool s_shellLoaded = false;
    if (!s_shellLoaded)
    {
        PlasmaMaterialParams p = {0};
        p.baseColor = (Color){20, 60, 160, 255};
        p.wispColor = (Color){110, 220, 255, 255};
        p.noiseScale = 3.2f;
        p.noiseSpeed = 0.45f;
        p.fresnelPower = 2.6f;
        p.rimStrength = 0.8f;
        p.emissive = 0.25f;
        p.opacity = 0.55f;
        p.displaceAmp = 0.05f;
        PlasmaMaterial_Load(&s_shellOuter, &p);

        p.noiseScale = 4.6f;
        p.noiseSpeed = -0.6f;
        p.fresnelPower = 2.0f;
        p.rimStrength = 0.5f;
        p.emissive = 0.15f;
        p.opacity = 0.3f;
        PlasmaMaterial_Load(&s_shellInner, &p);
        s_shellLoaded = true;
    }

    s_shellOuter.params.displaceAmp = r * 0.08f;
    s_shellInner.params.displaceAmp = r * 0.05f;

    PlasmaMaterial_Begin(s_shellOuter);
    DrawCoreSphere(pos, r, 20, 20, WHITE);
    PlasmaMaterial_End();

    PlasmaMaterial_Begin(s_shellInner);
    DrawCoreSphere(pos, r * 0.82f, 16, 16, WHITE);
    PlasmaMaterial_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    float lightSpawnRate = 10.0f;
    if (Random01() < (lightSpawnRate * dt))
    {
        VFXLight_Spawn(pos, (Color){90, 210, 255, 255},
                       r * (3.5f + 0.6f * sinf(time * 3.0f)), 0.15f, VFX_PRIORITY_LOW);
    }

    float distortSpawnRate = 2.5f;
    if (Random01() < (distortSpawnRate * dt))
    {
        ScreenDistort_Add(pos, r * 1.5f, 0.08f, 0.4f, 1.5f);
    }
}
