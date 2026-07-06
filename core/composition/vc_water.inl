void VFX_ComposeMagicPuddle(Vector3 pos)
{
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    float radius = 1.2f;
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.01f, pos.z);

    Shader flowShader = ResourceManager_LoadShader(0, "core/shaders/puddle.fs");
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    Texture2D flowTex = ResourceManager_LoadTexture("assets/textures/water_flow.png");

    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(flowTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(flowTex, TEXTURE_FILTER_BILINEAR);

    int timeLoc = GetShaderLocation(flowShader, "u_time");
    int tex0Loc = GetShaderLocation(flowShader, "causticsTex");
    int tex1Loc = GetShaderLocation(flowShader, "flowTex");

    float time = GetTime();
    SetShaderValue(flowShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(flowShader);
    SkillManager_BeginShader(flowShader);
    SetShaderValueTexture(flowShader, tex0Loc, tex);
    SetShaderValueTexture(flowShader, tex1Loc, flowTex);

    rlDrawRenderBatchActive();
    rlActiveTextureSlot(1);
    rlEnableTexture(flowTex.id);
    rlActiveTextureSlot(0);
    rlEnableTexture(tex.id);

    ProceduralMesh_DrawOrganicPuddle((Vector3){0, 0, 0}, radius);

    rlDrawRenderBatchActive();
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    rlDisableTexture();

    SkillManager_EndShader();
    EndShaderMode();

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time)
{
    static Shader s_tubeShader = {0};
    static int s_timeLoc = -1;
    static int s_viewPosLoc = -1;
    static int s_lightDirLoc = -1;
    static int s_uvLengthLoc = -1;

    if (s_tubeShader.id == 0)
    {
        s_tubeShader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
        s_timeLoc = GetShaderLocation(s_tubeShader, "u_time");
        s_viewPosLoc = GetShaderLocation(s_tubeShader, "viewPos");
        s_lightDirLoc = GetShaderLocation(s_tubeShader, "u_lightDir");
        s_uvLengthLoc = GetShaderLocation(s_tubeShader, "u_uvLength");
    }

    if (s_tubeShader.locs == NULL)
        return;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_tubeShader);

    SkillManager_BeginShader(s_tubeShader);

    SetShaderValue(s_tubeShader, s_timeLoc, &time, SHADER_UNIFORM_FLOAT);

    SetShaderValue(s_tubeShader, s_viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_tubeShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 2.0f;
    SetShaderValue(s_tubeShader, s_uvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

    rlColor4ub(255, 255, 255, 255);

    TubeMeshData mesh;
    TubeMeshConfig config = ProceduralMesh_DefaultTubeConfig();
    ProceduralMesh_BuildTube(&mesh, p0, p1, p2, p3, radius,
                             progress, time, 24, 16, &config);
    ProceduralMesh_DrawTube(&mesh, 2.0f);

    SkillManager_EndShader();
    EndShaderMode();
    EndBlendMode();
    rlDisableBackfaceCulling();
    rlEnableDepthMask();
}

void VFX_ComposeIceCrystal(Vector3 basePos, int seed)
{
    static CrystalMaterial s_iceMat;
    static bool s_iceMatLoaded = false;
    if (!s_iceMatLoaded)
    {
        CrystalMaterialParams p = {0};
        p.baseColor = (Color){0, 110, 230, 200};
        p.edgeColor = (Color){130, 220, 255, 255};
        p.roughness = 0.35f;
        p.fresnel = 1.2f;
        p.refraction = 0.15f;
        p.sparkle = 0.45f;
        p.crack = 0.35f;
        p.emission = 0.15f;
        p.thickness = 1.8f;
        s_iceMat = CrystalMaterial_Load(p);
        s_iceMatLoaded = true;
    }

    CrystalDesc desc = {0};
    desc.height = 1.3f;
    desc.radius = 0.16f;
    desc.taper = 0.85f;
    desc.twist = 0.6f;
    desc.noise = 0.08f;
    desc.sides = 6;
    desc.segments = 6;

    CrystalMaterial_Begin(s_iceMat);
    ProceduralMesh_DrawCrystalCluster(basePos, &desc, 3, seed, 1.0f, WHITE);
    CrystalMaterial_End();
}

// --- Water skill set ---------------------------------------------------------
// Water's read is WEIGHT + SOFTNESS together: droplets are heavy (real
// gravity, crown arcs, they FALL), everything around them is soft (mist,
// ripples, caustic shimmer). Three pieces water skills keep needing:
//   SplashBurst  — one-shot crown splash (projectile hit, landing)
//   BubbleStream — continuous rising bubbles (channel, submerged buff)
//   MistVeil     — continuous low fog bank (zone, concealment)

static ColorGradient s_dropGrad = {0};   // droplet body: sky-lit blue → deep fade
static ColorGradient s_dropCapGrad = {0}; // white foam caps
static ColorGradient s_mistGrad = {0};   // soft desaturated vapor
static ColorGradient s_bubbleGrad = {0}; // pale translucent bubble
static SkillCurve s_mistShape = {0};     // billow: grow while fading
static SkillCurve s_softInOut = {0};     // fade-in/out, no popping
static bool s_waterFxInit = false;

static void WaterFx_InitShared(void)
{
    if (s_waterFxInit)
        return;
    ColorGradient_AddStop(&s_dropGrad, 0.0f, (Color){120, 200, 255, 240});
    ColorGradient_AddStop(&s_dropGrad, 0.5f, (Color){41, 128, 185, 200});
    ColorGradient_AddStop(&s_dropGrad, 1.0f, (Color){15, 60, 110, 0});

    ColorGradient_AddStop(&s_dropCapGrad, 0.0f, (Color){235, 250, 255, 255});
    ColorGradient_AddStop(&s_dropCapGrad, 0.4f, (Color){170, 225, 255, 220});
    ColorGradient_AddStop(&s_dropCapGrad, 1.0f, (Color){60, 130, 190, 0});

    ColorGradient_AddStop(&s_mistGrad, 0.0f, (Color){150, 190, 215, 0});
    ColorGradient_AddStop(&s_mistGrad, 0.3f, (Color){140, 180, 205, 90});
    ColorGradient_AddStop(&s_mistGrad, 1.0f, (Color){100, 130, 155, 0});

    ColorGradient_AddStop(&s_bubbleGrad, 0.0f, (Color){140, 210, 255, 0});
    ColorGradient_AddStop(&s_bubbleGrad, 0.2f, (Color){170, 230, 255, 150});
    ColorGradient_AddStop(&s_bubbleGrad, 0.85f, (Color){200, 245, 255, 170});
    ColorGradient_AddStop(&s_bubbleGrad, 1.0f, (Color){255, 255, 255, 0});

    FloatCurve_AddStop(&s_mistShape, 0.0f, 0.5f);
    FloatCurve_AddStop(&s_mistShape, 1.0f, 1.7f);

    FloatCurve_AddStop(&s_softInOut, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_softInOut, 0.2f, 1.0f);
    FloatCurve_AddStop(&s_softInOut, 0.75f, 0.9f);
    FloatCurve_AddStop(&s_softInOut, 1.0f, 0.0f);

    s_waterFxInit = true;
}

void VFX_ComposeSplashBurst(Vector3 pos, float scale)
{
    WaterFx_InitShared();

    // Real gravity — water is the one element where "heavy" IS the read.
    static ForceField s_dropFld = {0};
    if (s_dropFld.layerCount == 0)
        ForceField_AddLayer(&s_dropFld, (ForceLayer){
                                            .type = FORCE_GRAVITY_DIR,
                                            .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                            .strength = 9.8f});

    // ① Crown ring — droplets ejected steep (55°–75°) in a ring, arcing over
    // and raining back down. ~20% white foam caps ride the top of the crown.
    int dropCount = 16 + GetRandomValue(0, 6);
    for (int i = 0; i < dropCount; i++)
    {
        float a = ((float)i / (float)dropCount) * 2.0f * PI + Random01() * 0.3f;
        float pitch = (55.0f + Random01() * 20.0f) * DEG2RAD;
        Vector3 dir = {cosf(a) * cosf(pitch), sinf(pitch), sinf(a) * cosf(pitch)};
        bool cap = GetRandomValue(0, 100) < 20;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.04f * scale)),
            .velocity = Vector3Scale(dir, (1.4f + Random01() * 0.9f) * scale),
            .radius = (cap ? 0.016f : 0.011f) * scale * (0.8f + Random01() * 0.4f),
            .lifetime = 0.45f + Random01() * 0.25f,
            .gradient = cap ? &s_dropCapGrad : &s_dropGrad,
            .forceField = &s_dropFld});
    }

    // ② Center jet — a few fast vertical droplets, the "plume" above the crown.
    for (int i = 0; i < 5; i++)
        SpawnParticle((ParticleConfig){
            .position = pos,
            .velocity = (Vector3){(Random01() - 0.5f) * 0.4f,
                                  (2.2f + Random01() * 0.8f) * scale,
                                  (Random01() - 0.5f) * 0.4f},
            .radius = 0.013f * scale,
            .lifetime = 0.5f + Random01() * 0.2f,
            .gradient = &s_dropCapGrad,
            .forceField = &s_dropFld});

    // ③ Mist puff — soft vapor hanging where the water tore apart.
    for (int i = 0; i < 6; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.1f * scale, pos.y + 0.05f,
                                  pos.z + sinf(a) * 0.1f * scale},
            .velocity = (Vector3){cosf(a) * 0.2f, 0.15f + Random01() * 0.1f, sinf(a) * 0.2f},
            .radius = (0.05f + Random01() * 0.04f) * scale,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_mistGrad,
            .radiusCurve = &s_mistShape});
    }

    // ④ Rings on the ground — crisp splash ring expanding fast + a softer
    // ripple lagging behind it (two speeds = liquid, one ring = stamp).
    Texture2D splashTex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
    Texture2D rippleTex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
    Vector3 ground = {pos.x, 0.0f, pos.z};
    DecalSystem_AddEx(ground, (float)GetRandomValue(0, 360), 15.0f,
                      scale * 0.15f, scale * 0.85f,
                      splashTex, 0.55f, ColorAlpha(VFX_Material(VC_MAT_WATER)->soft, 0.8f),
                      BLEND_ADDITIVE, 0.03f);
    DecalSystem_AddEx(ground, (float)GetRandomValue(0, 360), -8.0f,
                      scale * 0.1f, scale * 0.6f,
                      rippleTex, 0.9f, ColorAlpha(ELEMENT_COLOR_WATER, 0.5f),
                      BLEND_ADDITIVE, 0.02f);

    VFXLight_Spawn(pos, VFX_Material(VC_MAT_WATER)->soft, 1.6f * scale, 0.2f, VFX_PRIORITY_LOW);
    ScreenDistort_Add(pos, 0.4f * scale, 0.1f, 0.25f, 1.5f);
}

void VFX_ComposeBubbleStream(Vector3 pos, float radius, float time)
{
    WaterFx_InitShared();

    // Buoyancy + gentle side-to-side wobble — bubbles never rise straight.
    static ForceField s_bubbleFld = {0};
    if (s_bubbleFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_GRAVITY_DIR,
                                              .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                              .strength = 1.1f});
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_NOISE_CURL,
                                              .strength = 0.5f,
                                              .noiseScale = 4.0f,
                                              .noiseSpeed = 0.8f});
        ForceField_AddLayer(&s_bubbleFld, (ForceLayer){
                                              .type = FORCE_VISCOSITY,
                                              .strength = 1.8f}); // water drag — slow terminal rise
    }

    // Pop droplets — a bubble that dies bursts into 3 micro-droplets.
    static ParticleConfig s_popDrops;
    s_popDrops = (ParticleConfig){
        .velocity = (Vector3){0.0f, 0.08f, 0.0f},
        .radius = 0.005f,
        .lifetime = 0.22f,
        .gradient = &s_dropCapGrad};

    // ~1 bubble/frame-ish; brightening (gradient) toward end of life = the
    // bubble nearing the surface, then the onDeath pop.
    if (GetRandomValue(0, 100) < 55)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * 0.5f * sqrtf(Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.03f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, 0.15f + Random01() * 0.1f, 0},
            .radius = 0.007f + Random01() * 0.009f,
            .lifetime = 1.0f + Random01() * 0.8f,
            .gradient = &s_bubbleGrad,
            .radiusCurve = &s_softInOut,
            .forceField = &s_bubbleFld,
            .onDeathEmit = &s_popDrops,
            .onDeathEmitCount = 3});
    }

    // Caustic shimmer — faint light dancing as if refracted through water.
    if (GetRandomValue(0, 100) < 12)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){(Random01() - 0.5f) * radius,
                                                 radius * (0.4f + 0.4f * Random01()),
                                                 (Random01() - 0.5f) * radius}),
                       VFX_Material(VC_MAT_WATER)->soft, radius * 1.4f, 0.2f, VFX_PRIORITY_LOW);

    // Wet ground sheen under the column.
    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D rippleTex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
        DecalSystem_AddEx(pos, (float)GetRandomValue(0, 360), 5.0f,
                          radius * 0.7f, radius * 0.9f,
                          rippleTex, 1.4f, ColorAlpha(ELEMENT_COLOR_WATER, 0.35f),
                          BLEND_ADDITIVE, 0.015f);
    }

    (void)time;
}

void VFX_ComposeMistVeil(Vector3 pos, float radius, float time)
{
    WaterFx_InitShared();

    // Lazy rotation around the zone + strong damping: fog crawls, never blows.
    static ForceField s_mistFld;
    ForceField_Clear(&s_mistFld);
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_VORTEX,
                                        .origin = pos,
                                        .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                        .strength = 0.5f,
                                        .radius = radius * 2.0f,
                                        .falloff = 1.0f});
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_NOISE_CURL,
                                        .strength = 0.35f,
                                        .noiseScale = 1.8f,
                                        .noiseSpeed = 0.5f});
    ForceField_AddLayer(&s_mistFld, (ForceLayer){
                                        .type = FORCE_VISCOSITY,
                                        .strength = 2.2f});

    // Fog bodies — big, dim, slow, hugging the ground (y < 0.35·radius).
    // Billowing size curve + alpha-soft gradient reads as volume, not sprites.
    if (GetRandomValue(0, 100) < 65)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * (0.3f + 0.7f * Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr,
                                  pos.y + 0.05f + Random01() * radius * 0.3f,
                                  pos.z + sinf(a) * rr},
            .velocity = (Vector3){-sinf(a) * 0.1f, 0.02f, cosf(a) * 0.1f},
            .radius = (0.06f + Random01() * 0.06f) * (radius / 1.0f),
            .lifetime = 1.4f + Random01() * 1.0f,
            .gradient = &s_mistGrad,
            .radiusCurve = &s_mistShape,
            .forceField = &s_mistFld});
    }

    // Stray droplets condensing out of the fog and falling — ties the veil
    // to water instead of generic smoke.
    if (GetRandomValue(0, 100) < 10)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01() * 0.8f;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + radius * 0.35f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){0, -0.1f, 0},
            .radius = 0.006f + Random01() * 0.004f,
            .lifetime = 0.5f + Random01() * 0.3f,
            .gradient = &s_dropGrad,
            .radiusCurve = &s_softInOut});
    }

    // Cold moonlit sheen breathing over the bank.
    if (GetRandomValue(0, 100) < 8)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.25f, 0}),
                       VFX_Material(VC_MAT_WATER)->soft, radius * 1.6f,
                       0.3f, VFX_PRIORITY_LOW);

    (void)time;
}
