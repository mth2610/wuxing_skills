// Plasma Energy Orb — a contained ball of raw energy: hot white-cyan core,
// two counter-scrolling wispy noise membranes (PlasmaMaterial), and pink
// filament arcs crawling from the core toward the shell interior. Continuous
// archetype: call once per frame with a running `time`, like the other
// mesh-style composers (VFX_ComposeFireball, VFX_ComposeBladeRing...).

// One filament endpoint on the shell interior, deterministic per (index, epoch)
// so the strand pattern holds for a beat, then re-seeds — plasma-globe crawl
// instead of pure per-frame noise.
static Vector3 PlasmaFilamentDir(int index, int epoch)
{
    unsigned int rng = (unsigned int)(index * 374761393 + epoch * 668265263) + 1442695040u;
    rng ^= rng >> 13;
    rng *= 1274126177u;
    rng ^= rng >> 16;
    float u = (float)(rng & 0xFFFF) / 65535.0f; // yaw 0..1
    rng = rng * 1664525u + 1013904223u;
    float v = (float)(rng >> 8 & 0xFFFF) / 65535.0f; // pitch 0..1
    float yaw = u * 2.0f * PI;
    float pitch = acosf(2.0f * v - 1.0f) - PI * 0.5f; // uniform over the sphere
    return (Vector3){cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
}

void VFX_ComposePlasmaOrb(Vector3 pos, float radius, float time)
{
    if (radius <= 0.0f)
        return;

    // Slow breathing so the whole orb feels alive even before the shell noise reads.
    float breathe = 1.0f + 0.03f * sinf(time * 2.1f) + 0.015f * sinf(time * 5.3f);
    float r = radius * breathe;

    // ── Layer 1: core — ONE soft bloom sphere. A single translucent cyan
    // ball whose fresnel edge dissolves into the shell; no white heart disc
    // inside it (two nested spheres read as two separate circles).
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    EffectMaterialParams bloomParams = {0};
    bloomParams.baseColor = (Color){130, 215, 255, 70};
    bloomParams.emissiveIntensity = 0.8f + 0.15f * sinf(time * 5.0f);
    bloomParams.rimStrength = 0.4f;
    bloomParams.fresnelPower = 1.4f;
    bloomParams.translucency = 0.9f;
    EffectMaterial bloomMat = Material_LoadCustom(bloomParams);
    Material_Begin(bloomMat);
    DrawCoreSphere(pos, r * 0.30f, 16, 16, WHITE);
    Material_End();
    rlDrawRenderBatchActive();

    // ── Layer 2: wisp filaments — thin pink trails launched from the core,
    // writhing outward under a strong curl-noise field. Each is a fast head
    // particle dragging a dense tail of tiny particles (the shared round
    // texture can't stretch — the tail supplies the strand, same trick as
    // VFX_ComposeGlintBurst's long sparks).
    static ColorGradient s_wispHeadGrad = {0};
    static ColorGradient s_wispTailGrad = {0};
    static ForceField s_wispCurlFld = {0};
    static bool s_wispInit = false;
    if (!s_wispInit)
    {
        // Hot pink-white head cooling through pink into violet nothing.
        ColorGradient_AddStop(&s_wispHeadGrad, 0.0f, (Color){255, 235, 240, 255});
        ColorGradient_AddStop(&s_wispHeadGrad, 0.45f, (Color){255, 120, 165, 220});
        ColorGradient_AddStop(&s_wispHeadGrad, 1.0f, (Color){170, 60, 220, 0});

        ColorGradient_AddStop(&s_wispTailGrad, 0.0f, (Color){255, 150, 185, 190});
        ColorGradient_AddStop(&s_wispTailGrad, 1.0f, (Color){110, 45, 200, 0});

        // Strong curl bends the strands into loops; light viscosity keeps
        // them from being flung out through the membrane. noiseScale must be
        // high enough that the curl VARIES across the orb's ~1m interior —
        // orb-sized wavelengths shove every wisp the same way (they all
        // drift to one side instead of writhing independently).
        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL,
                                                .strength = 2.5f,
                                                .noiseScale = 3.5f,
                                                .noiseSpeed = 3.5f});
        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY,
                                                .strength = 2.5f});
        s_wispInit = true;
    }

    static ParticleConfig s_wispTail;
    s_wispTail = (ParticleConfig){
        .radius = 0.007f * (radius / 0.5f),
        .lifetime = 0.25f,
        .gradient = &s_wispTailGrad};

    // ~15 live strands. Budget check (MAX_PARTICLES = 2000): heads spawn at
    // 60fps × 45% ≈ 27/s × 0.55s life ≈ 15 live, tails 15 × 150/s × 0.25s
    // ≈ 560 live — the earlier dashed/dotted strands were the pool
    // saturating and dropping tail spawns in bursts, not an emit-rate issue
    // (onLiveEmit already interpolates along the frame's movement).
    if (GetRandomValue(0, 100) < 45)
    {
        // Fully random direction — the wisps fill the whole ball, no
        // quantized fan.
        Vector3 dir = PlasmaFilamentDir(GetRandomValue(0, 1023), GetRandomValue(0, 1023));
        float life = 0.45f + Random01() * 0.25f;
        // Slow drift; the curl field supplies most of the motion/writhe.
        float speed = r * 0.2f / life;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, r * 0.12f)),
            .velocity = Vector3Scale(dir, speed),
            .radius = 0.011f * (radius / 0.5f),
            .lifetime = life,
            .gradient = &s_wispHeadGrad,
            .forceField = &s_wispCurlFld,
            .onLiveEmit = &s_wispTail,
            .onLiveEmitRate = 150.0f});
    }

    // ── Layer 3: wispy membrane — the actual "ball" silhouette. Two shells,
    // different scales/phases: culling stays off so each also shows its far
    // hemisphere, stacking four sheets of drifting filament noise in depth.
    rlDisableBackfaceCulling();

    static PlasmaMaterial s_shellOuter, s_shellInner;
    static bool s_shellLoaded = false;
    if (!s_shellLoaded)
    {
        PlasmaMaterialParams p = {0};
        p.baseColor = (Color){20, 60, 160, 255};   // deep electric blue body
        p.wispColor = (Color){110, 220, 255, 255}; // bright cyan crests
        p.noiseScale = 3.2f;
        p.noiseSpeed = 0.45f;
        p.fresnelPower = 2.6f;
        p.rimStrength = 0.8f;
        p.emissive = 0.25f;
        p.opacity = 0.55f;
        p.displaceAmp = 0.05f; // scaled per-draw below via radius ratio
        s_shellOuter = PlasmaMaterial_Load(p);

        p.noiseScale = 4.6f;  // finer wisps inside
        p.noiseSpeed = -0.6f; // counter-scroll against the outer shell
        p.fresnelPower = 2.0f;
        p.rimStrength = 0.5f;
        p.emissive = 0.15f;
        p.opacity = 0.3f;
        s_shellInner = PlasmaMaterial_Load(p);
        s_shellLoaded = true;
    }

    // Displacement amplitude follows the orb's size (lumps ~8% of radius).
    s_shellOuter.params.displaceAmp = r * 0.08f;
    s_shellInner.params.displaceAmp = r * 0.05f;

    PlasmaMaterial_Begin(s_shellOuter);
    DrawCoreSphere(pos, r, 28, 28, WHITE);
    PlasmaMaterial_End();

    PlasmaMaterial_Begin(s_shellInner);
    DrawCoreSphere(pos, r * 0.82f, 24, 24, WHITE);
    PlasmaMaterial_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    // ── Layer 4: presence — light, stray motes, faint air-bend.
    if (GetRandomValue(0, 100) < 35)
        VFXLight_Spawn(pos, (Color){90, 210, 255, 255},
                       r * (3.5f + 0.6f * sinf(time * 3.0f)), 0.15f, VFX_PRIORITY_LOW);

    // Cyan motes shed off the membrane, drifting outward and dying.
    if (GetRandomValue(0, 100) < 30)
    {
        Vector3 dir = PlasmaFilamentDir(GetRandomValue(0, 63), GetRandomValue(0, 1023));
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, r * (0.95f + 0.1f * Random01()))),
            .velocity = Vector3Scale(dir, 0.15f + Random01() * 0.2f),
            .colorStart = (Color){140, 235, 255, 200},
            .colorEnd = (Color){40, 90, 255, 0},
            .radius = 0.008f + Random01() * 0.008f,
            .lifetime = 0.5f + Random01() * 0.5f});
    }

    // Rare hot spark where a filament kisses the membrane.
    if (GetRandomValue(0, 100) < 8)
    {
        Vector3 dir = PlasmaFilamentDir(GetRandomValue(0, 63), GetRandomValue(0, 1023));
        VFX_ComposeGlintBurst(Vector3Add(pos, Vector3Scale(dir, r * 0.85f)), 2,
                              r * 0.06f, (Color){255, 150, 190, 255});
    }

    // The air around a ball of plasma shouldn't be perfectly still.
    if (GetRandomValue(0, 100) < 6)
        ScreenDistort_Add(pos, r * 1.5f, 0.08f, 0.4f, 1.5f);
}
