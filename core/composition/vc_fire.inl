void VFX_ComposeFireball(Vector3 pos, float time)
{
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    float radius = 0.16f;
    float breathe = 0.03f * sinf(time * 6.0f);
    Vector3 actualPos = Vector3Add(pos, (Vector3){0, 0.25f + breathe, 0});

    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){255, 210, 130, 255};
    coreParams.emissiveIntensity = 2.5f;
    EffectMaterial coreMat = Material_LoadCustom(coreParams);
    Material_Begin(coreMat);
    DrawCoreSphere(actualPos, (radius * 0.55f), 16, 16, WHITE);
    Material_End();

    EffectMaterialParams auraParams = {0};
    auraParams.baseColor = (Color){240, 80, 10, 160};
    auraParams.rimStrength = 2.2f;
    auraParams.fresnelPower = 2.5f;
    auraParams.emissiveIntensity = 1.2f;
    auraParams.distortionStrength = 0.45f;
    auraParams.translucency = 0.6f;
    EffectMaterial auraMat = Material_LoadCustom(auraParams);
    Material_Begin(auraMat);
    DrawCoreSphere(actualPos, radius, 16, 16, WHITE);
    Material_End();
    rlEnableDepthMask();
    EndBlendMode();
}

void VFX_ComposeFlameWisp(Vector3 pos, float time)
{
    float radius = 0.08f;
    // Phase offset by position so multiple wisps in one cluster don't bob or
    // flicker in lockstep. Two incommensurate sines = an organic, non-looping
    // flicker rather than a clean pulse.
    float phase = pos.x * 4.0f + pos.z * 2.7f;
    float bob = 0.04f * sinf(time * 3.0f + phase);
    float flicker = 0.75f + 0.25f * sinf(time * 11.0f + phase) * sinf(time * 7.3f + phase * 1.7f);
    // Vertical stretch pulses with the flicker — a flame reaches upward when
    // it flares, it doesn't inflate uniformly like a balloon.
    float stretch = 1.45f + 0.35f * flicker;
    float sway = 0.02f * sinf(time * 5.0f + phase * 1.3f);
    Vector3 actualPos = Vector3Add(pos, (Vector3){sway, bob, 0});

    // Ambient smoke cap — faint dark puff hanging above the flame tip,
    // alpha-blended (smoke absorbs light, additive would make it glow).
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    EffectMaterialParams sp = {0};
    sp.baseColor = (Color){45, 38, 35, 60};
    sp.distortionStrength = 0.5f;
    sp.translucency = 0.7f;
    EffectMaterial smokeMat = Material_LoadCustom(sp);
    Material_Begin(smokeMat);
    DrawCoreSphere(Vector3Add(actualPos, (Vector3){sway * 2.0f, radius * stretch * 1.05f, 0}),
                   radius * 0.55f, 8, 8, WHITE);
    Material_End();
    EndBlendMode();

    BeginBlendMode(BLEND_ADDITIVE);

    // Teardrop flame body — a Y-stretched sphere, wobbling, flicker-driven.
    // The stretch matrix turns the shared sphere into a licking tongue of
    // flame instead of a glowing ball.
    EffectMaterialParams p = {0};
    p.baseColor = (Color){255, 150, 40, 220};
    p.emissiveIntensity = 1.4f + 0.9f * flicker;
    p.rimStrength = 1.6f;
    p.fresnelPower = 2.5f;
    p.distortionStrength = 0.4f;
    EffectMaterial mat = Material_LoadCustom(p);
    Material_Begin(mat);
    rlPushMatrix();
    rlTranslatef(actualPos.x, actualPos.y, actualPos.z);
    rlScalef(0.85f, stretch, 0.85f);
    DrawCoreSphere((Vector3){0, 0, 0}, radius * (0.95f + 0.1f * flicker), 12, 12, WHITE);
    rlPopMatrix();
    Material_End();

    // Hot core — smaller, brighter, stretched harder and sitting low in the
    // flame (heat concentrates at the base of a real flame).
    EffectMaterialParams cp = {0};
    cp.baseColor = (Color){255, 225, 150, 255};
    cp.emissiveIntensity = 2.2f + flicker;
    EffectMaterial coreMat = Material_LoadCustom(cp);
    Material_Begin(coreMat);
    rlPushMatrix();
    rlTranslatef(actualPos.x, actualPos.y - radius * 0.15f, actualPos.z);
    rlScalef(0.7f, stretch * 1.15f, 0.7f);
    DrawCoreSphere((Vector3){0, 0, 0}, radius * 0.45f, 10, 10, WHITE);
    rlPopMatrix();
    Material_End();

    rlEnableDepthMask();
    EndBlendMode();

    // Highlight — the occasional single ember popping off the tip.
    if (GetRandomValue(0, 100) < 6)
        VFX_ComposeEmberDrift(Vector3Add(actualPos, (Vector3){0, radius * stretch, 0}),
                              radius * 0.5f, 1, (Color){255, 140, 45, 255});
}

void VFX_ComposeFirePillar(Vector3 basePos, float progress)
{
    if (progress <= 0.0f)
        return;

    rlDisableBackfaceCulling();
    float height = 1.6f;
    float baseRadius = 0.3f;
    float topRadius = baseRadius * 0.15f;
    float t = (float)GetTime();

    // Same smoothstep rise as VFX_ComposeStonePillar — column grows from the
    // ground instead of popping in at full height (12.4 No Visual Popping).
    float rise = progress * progress * (3.0f - 2.0f * progress);
    // The tip always trembles — a fire column never holds a clean point.
    float tipJx = 0.05f * sinf(t * 9.0f + basePos.x * 5.0f);
    float tipJz = 0.05f * cosf(t * 7.3f + basePos.z * 4.0f);
    Vector3 top = Vector3Add(basePos, (Vector3){tipJx, height * rise, tipJz});
    // Live flame flicker on brightness/width so the column churns.
    float flicker = 0.85f + 0.15f * sinf(t * 14.0f + basePos.x * 5.0f) * sinf(t * 9.0f);

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    // Outer flame envelope — wider, translucent, deep-orange, heavy vertex
    // wobble so the silhouette licks and churns instead of being a clean cone.
    EffectMaterialParams outerParams = {0};
    outerParams.baseColor = (Color){235, 90, 25, 150};
    outerParams.emissiveIntensity = 1.3f * flicker;
    outerParams.rimStrength = 2.0f;
    outerParams.fresnelPower = 2.2f;
    outerParams.distortionStrength = 0.6f;
    outerParams.translucency = 0.4f;
    EffectMaterial outerMat = Material_LoadCustom(outerParams);
    Material_Begin(outerMat);
    DrawCoreCylinder(basePos, Vector3Add(top, (Vector3){0, height * 0.12f, 0}),
                     baseRadius * 1.25f, topRadius * 2.0f, 18, WHITE);
    Material_End();

    // Mid body — the main bright orange flame.
    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){255, 160, 60, 255};
    coreParams.emissiveIntensity = 2.0f * flicker;
    coreParams.rimStrength = 1.2f;
    coreParams.fresnelPower = 2.0f;
    coreParams.distortionStrength = 0.35f;
    EffectMaterial coreMat = Material_LoadCustom(coreParams);
    Material_Begin(coreMat);
    DrawCoreCylinder(basePos, top, baseRadius, topRadius, 16, WHITE);
    Material_End();

    // Twisting flame tongues — two thin off-axis columns orbiting the core
    // in opposite phases. A bare cylinder is rotationally symmetric, so this
    // pair is what makes the pillar visibly SPIN instead of just glowing.
    EffectMaterialParams twistParams = {0};
    twistParams.baseColor = (Color){255, 190, 90, 200};
    twistParams.emissiveIntensity = 1.8f * flicker;
    twistParams.distortionStrength = 0.5f;
    EffectMaterial twistMat = Material_LoadCustom(twistParams);
    Material_Begin(twistMat);
    for (int k = 0; k < 2; k++)
    {
        float spin = t * 160.0f + k * 180.0f;
        rlPushMatrix();
        rlTranslatef(basePos.x, basePos.y, basePos.z);
        rlRotatef(spin, 0.0f, 1.0f, 0.0f);
        // Off-axis at the base, converging toward the axis at the tip —
        // the classic fire-vortex cone silhouette.
        DrawCoreCylinder((Vector3){baseRadius * 0.6f, 0, 0},
                         (Vector3){baseRadius * 0.15f, height * rise * 0.85f, 0},
                         baseRadius * 0.22f, topRadius * 0.6f, 10, WHITE);
        rlPopMatrix();
    }
    Material_End();

    // Inner white-hot spine — thin, blazing, sells the heat at the core.
    EffectMaterialParams hotParams = {0};
    hotParams.baseColor = (Color){255, 235, 170, 255};
    hotParams.emissiveIntensity = 2.6f;
    hotParams.distortionStrength = 0.25f;
    EffectMaterial hotMat = Material_LoadCustom(hotParams);
    Material_Begin(hotMat);
    DrawCoreCylinder(basePos, Vector3Lerp(basePos, top, 0.9f),
                     baseRadius * 0.45f, topRadius, 12, WHITE);
    Material_End();

    rlEnableDepthMask();
    EndBlendMode();
    rlEnableBackfaceCulling();

    // Ground fire — a licking flame skirt where the pillar meets the floor,
    // plus a slow-crawling burn glow. Gated: decals have their own lifetime,
    // spawning one every frame would flood the pool.
    if (GetRandomValue(0, 100) < 6)
    {
        Texture2D glowTex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
        DecalSystem_AddEx(basePos, (float)GetRandomValue(0, 360), 15.0f,
                          baseRadius * 1.6f, baseRadius * 2.4f,
                          glowTex, 0.6f, (Color){255, 110, 30, 140}, BLEND_ADDITIVE, 0.02f);
    }

    // Rising embers — sold as an environmental fire, not a decal.
    if (GetRandomValue(0, 100) < 30)
    {
        float a = Random01() * 2.0f * PI;
        float r = baseRadius * (0.3f + 0.7f * Random01());
        Vector3 emberPos = {basePos.x + cosf(a) * r,
                            basePos.y + Random01() * height * rise * 0.5f,
                            basePos.z + sinf(a) * r};
        VFX_ComposeEmberDrift(emberPos, baseRadius * 0.4f, 1, (Color){255, 130, 40, 255});
    }

    // Smoke crown — dark slow puffs shedding off the trembling tip.
    if (rise > 0.5f && GetRandomValue(0, 100) < 18)
    {
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(top, (Vector3){(Random01() - 0.5f) * 0.1f, 0.05f, (Random01() - 0.5f) * 0.1f}),
            .velocity = (Vector3){(Random01() - 0.5f) * 0.1f, 0.25f + Random01() * 0.2f, (Random01() - 0.5f) * 0.1f},
            .colorStart = (Color){60, 50, 45, 130},
            .colorEnd = (Color){30, 28, 26, 0},
            .radius = 0.05f + Random01() * 0.04f,
            .lifetime = 0.8f + Random01() * 0.6f});
    }

    // Heat shimmer above the flame column.
    if (GetRandomValue(0, 100) < 5)
        ScreenDistort_Add(Vector3Add(top, (Vector3){0, 0.15f, 0}), baseRadius * 1.8f, 0.08f, 0.8f, 1.2f);

    if (GetRandomValue(0, 100) < 12)
        VFXLight_Spawn(Vector3Add(basePos, (Vector3){0, 0.1f, 0}),
                       (Color){255, 120, 40, 255}, baseRadius * 4.0f * rise, 0.2f, VFX_PRIORITY_LOW);
}
