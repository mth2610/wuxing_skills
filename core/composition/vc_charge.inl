// Charge-up / Channel archetype — a windup effect: particles converge
// inward toward `pos` while a core orb grows, for cast/channel phases
// before a skill releases. `progress` 0..1 = windup, call once per frame.

static ForceField s_chargePullFld[6]; // one per ChargeStyle, lazy-built
static bool s_chargeFldInit[6] = {0};

static ForceField *ChargeGetPullField(ChargeStyle style, Vector3 pos, float pullStrength)
{
    ForceField *f = &s_chargePullFld[style];
    ForceField_Clear(f);
    ForceField_AddLayer(f, (ForceLayer){.type = FORCE_GRAVITY_POINT, .origin = pos, .strength = pullStrength, .radius = 5.0f, .falloff = 1.0f});
    ForceField_AddLayer(f, (ForceLayer){.type = FORCE_VORTEX, .origin = pos, .direction = (Vector3){0, 1, 0}, .strength = pullStrength * 0.5f, .radius = 5.0f, .falloff = 1.0f});
    s_chargeFldInit[style] = true;
    return f;
}

void VFX_ComposeChargeUp(ChargeStyle style, Vector3 pos, float radius, float progress, float time)
{
    const VFX_ElementMaterial *mat;
    Color color;
    switch (style)
    {
        case CHARGE_FIRE:  mat = VFX_Material(VC_MAT_FIRE);  color = mat->body; break;
        case CHARGE_METAL: mat = VFX_Material(VC_MAT_METAL); color = mat->glow; break; // electric blue-white
        case CHARGE_WATER: mat = VFX_Material(VC_MAT_WATER); color = mat->body; break;
        case CHARGE_WOOD:  mat = VFX_Material(VC_MAT_WOOD);  color = mat->body; break;
        case CHARGE_EARTH: mat = VFX_Material(VC_MAT_EARTH); color = mat->body; break;
        case CHARGE_TAIJI:
        default:           mat = VFX_Material(VC_MAT_TAIJI); color = mat->body; break;
    }
    const char *runePath = mat->runeDecal;

    progress = fminf(fmaxf(progress, 0.0f), 1.0f);
    if (progress <= 0.0f)
        return;

    // Core orb grows with progress, pulses faster as it nears release.
    float coreRadius = radius * 0.18f * (0.3f + 0.7f * progress);
    float pulseSpeed = 3.0f + progress * 6.0f;
    float pulse = VC_Breathe(time, pulseSpeed, 0.08f);

    // Shell tremble — the orb starts shaking as it struggles to contain the
    // energy (only past 60% so the early channel feels stable).
    float trembleAmt = fmaxf(progress - 0.6f, 0.0f) * 0.06f;
    Vector3 orbPos = VC_MotionJitter(pos, trembleAmt, 37.0f, time, 0.0f);

    // Ground rune — brightens and spins faster as the charge builds: the
    // caster's circle visibly "powering up" beneath the orb.
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    {
        Texture2D runeTex = ResourceManager_LoadTexture(runePath);
        float runeR = radius * (0.7f + 0.2f * progress);
        unsigned char runeA = (unsigned char)(40 + 170 * progress * progress);
        // Rune sits on the ground (project convention: Y=0 = ground level),
        // regardless of how high the orb itself is being channeled.
        VC_DrawGroundRune(runeTex, (Vector3){pos.x, 0.015f, pos.z}, runeR,
                          time * (15.0f + 45.0f * progress), VC_WithAlpha(color, runeA));
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();

    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = ColorAlpha(WHITE, 0.9f);
    coreParams.emissiveIntensity = 1.5f + progress * 1.5f;
    EffectMaterial coreMat = Material_LoadCustom(coreParams);
    Material_Begin(coreMat);
    DrawCoreSphere(orbPos, coreRadius * pulse * 0.5f, 14, 14, WHITE);
    Material_End();

    EffectMaterialParams auraParams = {0};
    auraParams.baseColor = ColorAlpha(color, 0.7f);
    auraParams.rimStrength = 2.0f + progress;
    auraParams.fresnelPower = 2.5f;
    auraParams.emissiveIntensity = 1.0f + progress;
    auraParams.distortionStrength = 0.3f + progress * 0.3f;
    auraParams.translucency = 0.5f;
    EffectMaterial auraMat = Material_LoadCustom(auraParams);
    Material_Begin(auraMat);
    DrawCoreSphere(orbPos, coreRadius * pulse, 14, 14, WHITE);
    Material_End();

    // Orbit ring — motes circling the orb, tightening and accelerating with
    // progress: the gathered energy falling into orbit before release.
    EffectMaterialParams orbitParams = {0};
    orbitParams.baseColor = ColorAlpha(color, 0.9f);
    orbitParams.emissiveIntensity = 1.6f + progress;
    EffectMaterial orbitMat = Material_LoadCustom(orbitParams);
    Material_Begin(orbitMat);
    int moteCount = 3 + (int)(progress * 2.0f);
    for (int i = 0; i < moteCount; i++)
    {
        float oa = time * (2.0f + progress * 4.0f) + (float)i * (2.0f * PI / (float)moteCount);
        float orbR = coreRadius * (2.6f - 0.8f * progress);
        Vector3 op = VC_RingPointXZ(orbPos, orbR, oa);
        op.y += sinf(oa * 2.3f + i) * orbR * 0.3f;
        DrawCoreSphere(op, coreRadius * 0.13f, 8, 8, WHITE);
    }
    Material_End();

    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    VFXLight_Spawn(pos, color, radius * (0.4f + 0.5f * progress), 0.15f, VFX_PRIORITY_LOW);

    // Air distortion — past 70% the atmosphere around the orb starts bending;
    // strength scales with how close release is.
    if (progress > 0.7f && GetRandomValue(0, 100) < 20)
        ScreenDistort_Add(orbPos, radius * 0.7f, 0.1f + 0.2f * (progress - 0.7f) / 0.3f, 0.3f, 2.0f);

    // Tiny lightning — near release, stray micro-arcs snap from the orbit
    // ring into the core: containment failing.
    if (progress > 0.75f && GetRandomValue(0, 100) < 25)
    {
        float la = Random01() * 2.0f * PI;
        float lr = coreRadius * 2.4f;
        Vector3 arcFrom = {orbPos.x + cosf(la) * lr, orbPos.y + (Random01() - 0.5f) * lr, orbPos.z + sinf(la) * lr};
        Vector3 wp[9];
        RegenerateLightningWaypoints(wp, arcFrom, orbPos, 0.15f);
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawLightningBolt(wp, 0.006f, camera);
        rlDrawRenderBatchActive();
        EndBlendMode();
    }

    // Fade in on spawn, then shrink to nothing as it reaches the core — reads
    // as energy being drawn in and absorbed rather than snapping out.
    static SkillCurve s_chargeMoteSize = {0};
    static bool s_chargeCurveInit = false;
    if (!s_chargeCurveInit)
    {
        FloatCurve_AddStop(&s_chargeMoteSize, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_chargeMoteSize, 0.3f, 1.0f);
        FloatCurve_AddStop(&s_chargeMoteSize, 0.85f, 0.9f);
        FloatCurve_AddStop(&s_chargeMoteSize, 1.0f, 0.0f);
        s_chargeCurveInit = true;
    }

    // Particles converge inward from an outer ring that shrinks as
    // progress advances — a visible "gathering energy" read. Spawn rate and
    // inward speed both ramp with progress so the pull intensifies near release.
    if (GetRandomValue(0, 100) < (int)(30 + 30 * progress))
    {
        float ringR = radius * (1.0f - 0.4f * progress);
        Vector3 spawnPos = VC_RingPointXZ(pos, ringR, Random01() * 2.0f * PI);
        spawnPos.y += Random01() * radius * 0.3f;
        Vector3 toCenter = Vector3Normalize(Vector3Subtract(pos, spawnPos));

        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = Vector3Scale(toCenter, 0.3f + progress * 0.5f),
            .colorStart = color,
            .colorEnd = ColorAlpha(WHITE, 0.0f),
            .radius = 0.012f + Random01() * 0.01f,
            .lifetime = 0.3f + Random01() * 0.2f,
            .radiusCurve = &s_chargeMoteSize,
            .forceField = ChargeGetPullField(style, pos, 2.0f + progress * 2.0f)});
    }

    // Energy peaking near release — occasional glint burst off the core.
    if (progress > 0.7f && GetRandomValue(0, 100) < 20)
        VFX_ComposeGlintBurst(pos, 3, coreRadius * 1.5f, color);
}
