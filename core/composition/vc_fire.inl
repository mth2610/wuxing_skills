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

// --- Particle-flow fire ------------------------------------------------------
// Fire here is NOT geometry. A mesh sphere/cylinder can only ever read as a
// glowing blob; real flame shape is an emergent property of a FLOW: many
// short-lived particles rising from a base disc, converging toward the axis
// (that convergence is what creates the teardrop/tongue silhouette), growing
// briefly, then shrinking to nothing while their colour cools through the
// blackbody ramp white → yellow → orange → deep red → gone. Each particle is
// one packet of burning gas; the flame is the crowd.

// Blackbody cooling ramp shared by all flame particles (built once).
static ColorGradient s_fireBodyGrad = {0};  // outer tongues: yellow → orange → red
static ColorGradient s_fireCoreGrad = {0};  // inner jet: white → yellow → orange
static ColorGradient s_fireSmokeGrad = {0}; // combustion product: warm gray → cold dark
static SkillCurve s_flameShape = {0};       // grow fast, shrink to a point (tongue taper)
static SkillCurve s_smokeShape = {0};       // smoke expands as it rises
static bool s_fireGradInit = false;

static ForceField s_flameFld = {0}; // buoyancy + curl licking, shared by all fire

static void FireFlow_InitShared(void)
{
    if (s_fireGradInit)
        return;

    ColorGradient_AddStop(&s_fireBodyGrad, 0.0f, (Color){255, 235, 170, 235});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.25f, (Color){255, 180, 70, 220});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.6f, (Color){235, 90, 20, 150});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.85f, (Color){130, 35, 12, 60});
    ColorGradient_AddStop(&s_fireBodyGrad, 1.0f, (Color){60, 20, 10, 0});

    ColorGradient_AddStop(&s_fireCoreGrad, 0.0f, (Color){255, 255, 235, 255});
    ColorGradient_AddStop(&s_fireCoreGrad, 0.35f, (Color){255, 230, 130, 230});
    ColorGradient_AddStop(&s_fireCoreGrad, 0.75f, (Color){255, 150, 45, 120});
    ColorGradient_AddStop(&s_fireCoreGrad, 1.0f, (Color){200, 80, 20, 0});

    ColorGradient_AddStop(&s_fireSmokeGrad, 0.0f, (Color){85, 70, 60, 0});
    ColorGradient_AddStop(&s_fireSmokeGrad, 0.25f, (Color){70, 60, 55, 90});
    ColorGradient_AddStop(&s_fireSmokeGrad, 1.0f, (Color){35, 32, 30, 0});

    // Flame tongue: swells right after birth, then tapers to a point as it
    // cools — the shrink is what draws the flame's tip.
    FloatCurve_AddStop(&s_flameShape, 0.0f, 0.55f);
    FloatCurve_AddStop(&s_flameShape, 0.2f, 1.0f);
    FloatCurve_AddStop(&s_flameShape, 0.65f, 0.6f);
    FloatCurve_AddStop(&s_flameShape, 1.0f, 0.0f);

    // Smoke: opposite — starts small, billows outward as it rises.
    FloatCurve_AddStop(&s_smokeShape, 0.0f, 0.4f);
    FloatCurve_AddStop(&s_smokeShape, 1.0f, 1.6f);

    // Buoyancy (hot gas accelerates upward while it lives) + curl noise
    // (the lick). The upward force is what makes tongues STRETCH mid-life
    // instead of coasting at spawn speed — dead giveaway of cheap fire.
    ForceField_AddLayer(&s_flameFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 2.2f});
    ForceField_AddLayer(&s_flameFld, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = 0.55f,
                                         .noiseScale = 0.9f,
                                         .noiseSpeed = 2.2f});

    s_fireGradInit = true;
}

// Emit one burning-gas packet. axisPos = where the flame axis crosses the
// spawn disc; r/angle = where on the disc this packet ignites; converge>0
// pulls its velocity toward the axis as it rises (tongue taper), height sets
// how far it lives upward, hot = inner-jet colouring.
static void FireFlow_EmitPacket(Vector3 axisPos, float discR, float height,
                                float converge, bool hot, float sizeScale)
{
    float a = Random01() * 2.0f * PI;
    float r = discR * sqrtf(Random01()); // sqrt = uniform over the disc area
    Vector3 spawn = {axisPos.x + cosf(a) * r, axisPos.y, axisPos.z + sinf(a) * r};

    float life = (0.3f + Random01() * 0.25f) * (0.7f + 0.5f * height);
    float upSpeed = (height / fmaxf(life, 0.05f)) * (0.75f + Random01() * 0.4f);

    // Radial-inward velocity so the packet drifts toward the axis while it
    // rises — the crowd of packets forms a cone tapering to a tip with no
    // mesh ever drawn.
    Vector3 inward = {-cosf(a) * r * converge / fmaxf(life, 0.05f), upSpeed,
                      -sinf(a) * r * converge / fmaxf(life, 0.05f)};

    SpawnParticle((ParticleConfig){
        .position = spawn,
        .velocity = inward,
        .radius = (hot ? 0.030f : 0.045f) * sizeScale * (0.8f + Random01() * 0.45f),
        .lifetime = life,
        .gradient = hot ? &s_fireCoreGrad : &s_fireBodyGrad,
        .radiusCurve = &s_flameShape,
        .forceField = &s_flameFld});
}

void VFX_ComposeFlameWisp(Vector3 pos, float time)
{
    FireFlow_InitShared();

    // Per-wisp breathing: spawn point and intensity drift with position-keyed
    // phases so clustered wisps never sync.
    float phase = pos.x * 4.0f + pos.z * 2.7f;
    float flare = 0.7f + 0.3f * sinf(time * 6.0f + phase) * sinf(time * 4.1f + phase * 1.7f);
    Vector3 base = {pos.x + 0.015f * sinf(time * 3.0f + phase), pos.y, pos.z};

    // Body tongues — ~2/frame keeps a standing population of ~25 packets:
    // enough for a solid little flame, cheap enough for a dozen wisps.
    FireFlow_EmitPacket(base, 0.045f, 0.16f + 0.06f * flare, 0.85f, false, 1.0f);
    if (GetRandomValue(0, 100) < 75)
        FireFlow_EmitPacket(base, 0.045f, 0.16f + 0.06f * flare, 0.85f, false, 1.0f);

    // Inner jet — white-hot packet up the middle, less often.
    if (GetRandomValue(0, 100) < 55)
        FireFlow_EmitPacket(base, 0.015f, 0.19f + 0.07f * flare, 0.5f, true, 0.9f);

    // Combustion smoke — a faint puff continuing where the tongues died.
    if (GetRandomValue(0, 100) < 10)
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(base, (Vector3){0, 0.2f + 0.05f * flare, 0}),
            .velocity = (Vector3){(Random01() - 0.5f) * 0.05f, 0.2f + Random01() * 0.1f, (Random01() - 0.5f) * 0.05f},
            .radius = 0.03f,
            .lifetime = 0.7f + Random01() * 0.4f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    // A stray ember snapping off the tip.
    if (GetRandomValue(0, 100) < 5)
        VFX_ComposeEmberDrift(Vector3Add(base, (Vector3){0, 0.15f, 0}), 0.04f, 1,
                              VFX_Material(VC_MAT_FIRE)->soft);

    // Warm light breathing with the flare.
    if (GetRandomValue(0, 100) < 25)
        VFXLight_Spawn(Vector3Add(base, (Vector3){0, 0.08f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, 0.55f + 0.2f * flare, 0.15f, VFX_PRIORITY_LOW);
}

void VFX_ComposeFirePillar(Vector3 basePos, float progress)
{
    if (progress <= 0.0f)
        return;

    FireFlow_InitShared();

    float height = 1.6f;
    float baseRadius = 0.3f;
    float t = (float)GetTime();

    // Smoothstep rise — the flame column climbs as progress advances
    // (12.4 No Visual Popping) by simply emitting taller-living packets.
    float rise = progress * progress * (3.0f - 2.0f * progress);
    float liveHeight = height * rise;

    // Gust cycle — a real bonfire surges and settles on a 1–2s rhythm; every
    // emission count below breathes with it, so the column visibly ROARS
    // instead of streaming at a constant rate.
    float gust = 0.75f + 0.25f * sinf(t * 3.1f + basePos.x * 2.0f) * sinf(t * 1.7f + basePos.z);

    // --- Layer 1: flame body — the column itself. A rotating spawn arm
    // (like a vortex updraft feeding the fire) sweeps around the base disc:
    // consecutive frames ignite neighbouring angles, so the crowd of packets
    // forms visible spiral tongues climbing the column with zero mesh.
    int bodyCount = 3 + (int)(3.0f * gust * rise);
    for (int i = 0; i < bodyCount; i++)
    {
        float armA = t * 5.5f + (float)i * (2.0f * PI / (float)bodyCount);
        float r = baseRadius * (0.35f + 0.65f * Random01());
        Vector3 spawn = {basePos.x + cosf(armA) * r, basePos.y, basePos.z + sinf(armA) * r};

        float life = 0.35f + Random01() * 0.3f;
        float upSpeed = liveHeight / fmaxf(life, 0.05f) * (0.8f + Random01() * 0.35f);
        // Tangential push (the updraft's swirl) + inward pull (the taper).
        Vector3 vel = {-sinf(armA) * 0.45f - cosf(armA) * r / life * 0.8f,
                       upSpeed,
                       cosf(armA) * 0.45f - sinf(armA) * r / life * 0.8f};

        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = vel,
            .radius = 0.07f * (0.75f + Random01() * 0.5f),
            .lifetime = life,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    // --- Layer 2: white-hot jet — fast, bright packets up the very centre.
    // The core outruns the body (hotter gas rises faster), which layers the
    // colours vertically exactly like a real flame cross-section.
    if (GetRandomValue(0, 100) < (int)(85 * gust))
        FireFlow_EmitPacket(basePos, baseRadius * 0.18f, liveHeight * 1.1f, 0.6f, true, 1.6f);

    // --- Layer 3: base skirt — slow fat tongues licking outward at the
    // floor, where the fire is widest and coolest. Grounds the column.
    if (GetRandomValue(0, 100) < (int)(60 * gust))
    {
        float a = Random01() * 2.0f * PI;
        Vector3 spawn = {basePos.x + cosf(a) * baseRadius, basePos.y + 0.02f,
                         basePos.z + sinf(a) * baseRadius};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = (Vector3){cosf(a) * 0.25f, 0.35f + Random01() * 0.2f, sinf(a) * 0.25f},
            .radius = 0.06f + Random01() * 0.03f,
            .lifetime = 0.25f + Random01() * 0.15f,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    // --- Layer 4: smoke column — combustion product continuing above where
    // the flame packets die, drifting with the same curl field.
    if (rise > 0.4f && GetRandomValue(0, 100) < (int)(30 * gust))
        SpawnParticle((ParticleConfig){
            .position = (Vector3){basePos.x + (Random01() - 0.5f) * baseRadius * 0.8f,
                                  basePos.y + liveHeight * (0.75f + Random01() * 0.3f),
                                  basePos.z + (Random01() - 0.5f) * baseRadius * 0.8f},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.15f, 0.35f + Random01() * 0.25f, (Random01() - 0.5f) * 0.15f},
            .radius = 0.07f + Random01() * 0.05f,
            .lifetime = 0.9f + Random01() * 0.7f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    // --- Layer 5: embers spat out of the column on the gust peaks.
    if (GetRandomValue(0, 100) < (int)(25 * gust))
    {
        float a = Random01() * 2.0f * PI;
        Vector3 emberPos = {basePos.x + cosf(a) * baseRadius * 0.5f,
                            basePos.y + Random01() * liveHeight * 0.6f,
                            basePos.z + sinf(a) * baseRadius * 0.5f};
        VFX_ComposeEmberDrift(emberPos, baseRadius * 0.4f, 1, VFX_Material(VC_MAT_FIRE)->soft);
    }

    // Ground glow — firelight pooling on the floor around the base.
    if (GetRandomValue(0, 100) < 6)
    {
        Texture2D glowTex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
        DecalSystem_AddEx(basePos, (float)GetRandomValue(0, 360), 15.0f,
                          baseRadius * 1.6f, baseRadius * 2.4f,
                          glowTex, 0.6f, VC_WithAlpha(VFX_Material(VC_MAT_FIRE)->soft, 140), BLEND_ADDITIVE, 0.02f);
    }

    // Heat shimmer above the column.
    if (GetRandomValue(0, 100) < 5)
        ScreenDistort_Add(Vector3Add(basePos, (Vector3){0, liveHeight + 0.15f, 0}),
                          baseRadius * 1.8f, 0.08f, 0.8f, 1.2f);

    // Firelight breathing with the gust.
    if (GetRandomValue(0, 100) < 20)
        VFXLight_Spawn(Vector3Add(basePos, (Vector3){0, liveHeight * 0.3f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, baseRadius * (3.0f + 2.0f * gust) * rise,
                       0.2f, VFX_PRIORITY_LOW);
}

// --- Fire skill set (Phase 2) --------------------------------------------
// Same philosophy as the flows above — fire is a crowd of burning-gas
// packets, never geometry. Three pieces fire skills keep needing:
//   FlameBreath   — continuous directional cone (flamethrower, dragon breath)
//   BurningGround — continuous ignited patch (ignite DoT, fire zone)
//   FireWhirl     — continuous fire tornado (ult centerpiece)

void VFX_ComposeFlameBreath(Vector3 pos, Vector3 dir, float scale, float time)
{
    FireFlow_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 up = fabsf(n.y) > 0.9f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Vector3 side = Vector3Normalize(Vector3CrossProduct(n, up));
    Vector3 side2 = Vector3CrossProduct(n, side);

    // Throat flutter — the stream surges on a fast rhythm; a constant-rate
    // jet reads as a gas burner, not breath.
    float surge = 0.7f + 0.3f * sinf(time * 9.0f) * sinf(time * 5.3f + 1.7f);

    // ── Stream body: packets blasted down a ~14° cone. The buoyancy in
    // s_flameFld makes the far end of the stream curl UPWARD as the packets
    // slow — the signature flamethrower arc, free of charge.
    int jetCount = 2 + (int)(2.0f * surge);
    for (int i = 0; i < jetCount; i++)
    {
        float spread = 0.25f * Random01();
        float ring = Random01() * 2.0f * PI;
        Vector3 d = Vector3Normalize(Vector3Add(n,
                        Vector3Add(Vector3Scale(side, cosf(ring) * spread),
                                   Vector3Scale(side2, sinf(ring) * spread))));
        bool hot = GetRandomValue(0, 100) < 35; // white-hot core rides the axis
        if (hot)
            d = Vector3Normalize(Vector3Add(n, Vector3Scale(d, 0.15f)));
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(d, 0.06f * scale)),
            .velocity = Vector3Scale(d, (2.6f + Random01() * 1.2f) * scale * surge),
            .radius = (hot ? 0.030f : 0.048f) * scale * (0.8f + Random01() * 0.45f),
            .lifetime = 0.30f + Random01() * 0.18f,
            .gradient = hot ? &s_fireCoreGrad : &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_flameFld});
    }

    // ── Combustion smoke where the stream dies (~1m out along dir).
    if (GetRandomValue(0, 100) < 25)
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(n, (0.8f + Random01() * 0.4f) * scale)),
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, 0.3f + Random01() * 0.2f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.05f * scale,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    // ── Heat shimmer wrapping the stream, gated.
    if (GetRandomValue(0, 100) < 10)
        ScreenDistort_Add(Vector3Add(pos, Vector3Scale(n, 0.6f * scale)),
                          0.5f * scale, 0.1f, 0.4f, 2.0f);

    // ── Muzzle light — brightest at the throat, surging with the breath.
    if (GetRandomValue(0, 100) < 30)
        VFXLight_Spawn(Vector3Add(pos, Vector3Scale(n, 0.2f * scale)),
                       VFX_Material(VC_MAT_FIRE)->soft, (1.4f + 0.6f * surge) * scale,
                       0.12f, VFX_PRIORITY_LOW);
}

void VFX_ComposeBurningGround(Vector3 pos, float radius, float time)
{
    FireFlow_InitShared();

    // ── Flame tongues at random points across the patch — clustered toward
    // the center (sqrt-less r = center-biased), each a short FireFlow packet.
    int tongues = 2 + GetRandomValue(0, 1);
    for (int i = 0; i < tongues; i++)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01() * Random01(); // center-biased
        Vector3 spot = {pos.x + cosf(a) * rr, pos.y, pos.z + sinf(a) * rr};
        FireFlow_EmitPacket(spot, 0.04f, 0.12f + 0.08f * Random01(), 0.8f,
                            GetRandomValue(0, 100) < 20, 0.9f);
    }

    // ── The patch itself: lava-crack glow pulsing under the flames + a
    // long-lived scorch accumulating at the rim. Both gated with matching
    // lifetimes so per-frame calls keep one of each alive.
    if (GetRandomValue(0, 100) < 3)
    {
        Texture2D lavaTex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 3.0f,
                          radius * 0.95f, radius * 1.05f,
                          lavaTex, 1.3f, ColorAlpha(VFX_Material(VC_MAT_FIRE)->soft, 0.7f),
                          BLEND_ADDITIVE, 0.02f);
    }
    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D scorchTex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
        DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, (float)GetRandomValue(0, 360),
                        radius * 1.15f, scorchTex, 3.0f, ColorAlpha(WHITE, 0.6f));
    }

    // ── Embers + smoke lifting off the patch.
    if (GetRandomValue(0, 100) < 15)
    {
        float a = Random01() * 2.0f * PI;
        VFX_ComposeEmberDrift((Vector3){pos.x + cosf(a) * radius * 0.5f, pos.y + 0.05f,
                                        pos.z + sinf(a) * radius * 0.5f},
                              radius * 0.3f, 1, VFX_Material(VC_MAT_FIRE)->soft);
    }
    if (GetRandomValue(0, 100) < 8)
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + (Random01() - 0.5f) * radius,
                                  pos.y + 0.25f,
                                  pos.z + (Random01() - 0.5f) * radius},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.1f, 0.3f + Random01() * 0.15f, (Random01() - 0.5f) * 0.1f},
            .radius = 0.045f + Random01() * 0.03f,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    // ── Firelight pooling over the patch, flickering.
    if (GetRandomValue(0, 100) < 22)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.2f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, radius * (2.0f + 0.6f * Random01()),
                       0.15f, VFX_PRIORITY_LOW);
}

void VFX_ComposeFireWhirl(Vector3 pos, float radius, float time)
{
    FireFlow_InitShared();

    float height = radius * 3.0f;

    // Tight vortex + strong updraft — hotter and faster than the wind
    // cyclone; origin tracks pos (single-static rebuild pattern).
    static ForceField s_whirlFld;
    ForceField_Clear(&s_whirlFld);
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_VORTEX,
                                         .origin = pos,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 7.0f,
                                         .radius = radius * 2.5f,
                                         .falloff = 1.0f});
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 3.0f});
    ForceField_AddLayer(&s_whirlFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_POINT,
                                         .origin = Vector3Add(pos, (Vector3){0, height * 0.6f, 0}),
                                         .strength = 2.0f,
                                         .radius = radius * 3.0f,
                                         .falloff = 1.0f});

    // ── Flame body: rotating spawn arm feeding tongues at the base — the
    // packets spiral up the funnel in visible burning bands.
    int arms = 3;
    for (int i = 0; i < arms; i++)
    {
        float armA = time * 8.0f + (float)i * (2.0f * PI / (float)arms);
        float rr = radius * (0.45f + 0.35f * Random01());
        Vector3 spawn = {pos.x + cosf(armA) * rr, pos.y + Random01() * height * 0.1f,
                         pos.z + sinf(armA) * rr};
        Vector3 tangent = {-sinf(armA) * 1.0f, 0.5f, cosf(armA) * 1.0f};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = tangent,
            .radius = 0.05f * (0.75f + Random01() * 0.5f),
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_fireBodyGrad,
            .radiusCurve = &s_flameShape,
            .forceField = &s_whirlFld});
    }

    // ── White-hot core jet straight up the axis — the whirl's spine.
    if (GetRandomValue(0, 100) < 70)
        FireFlow_EmitPacket(pos, radius * 0.15f, height * 0.6f, 0.5f, true, 1.4f);

    // ── Embers slung out of the funnel by centrifugal force.
    if (GetRandomValue(0, 100) < 30)
    {
        float a = Random01() * 2.0f * PI;
        Vector3 rim = {pos.x + cosf(a) * radius, pos.y + Random01() * height * 0.6f,
                       pos.z + sinf(a) * radius};
        SpawnParticle((ParticleConfig){
            .position = rim,
            .velocity = (Vector3){-sinf(a) * 1.8f, 0.4f, cosf(a) * 1.8f},
            .radius = 0.010f + Random01() * 0.008f,
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_fireCoreGrad,
            .forceField = &s_flameFld}); // hand off to plain buoyancy once flung
    }

    // ── Smoke crown above the funnel.
    if (GetRandomValue(0, 100) < 20)
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + (Random01() - 0.5f) * radius,
                                  pos.y + height * (0.8f + Random01() * 0.25f),
                                  pos.z + (Random01() - 0.5f) * radius},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.3f, 0.4f + Random01() * 0.3f, (Random01() - 0.5f) * 0.3f},
            .radius = 0.07f + Random01() * 0.05f,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_fireSmokeGrad,
            .radiusCurve = &s_smokeShape,
            .forceField = &s_flameFld});

    // ── Heat column + strong pulsing light — this is an ult, it owns the room.
    if (GetRandomValue(0, 100) < 12)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                          radius * 2.0f, 0.12f, 0.7f, 1.5f);
    if (GetRandomValue(0, 100) < 35)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, height * 0.4f, 0}),
                       VFX_Material(VC_MAT_FIRE)->soft, radius * (4.0f + Random01()),
                       0.15f, VFX_PRIORITY_LOW);
}
