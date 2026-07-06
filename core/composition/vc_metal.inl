// Metal-specific procedural visual components (implementations)

void VFX_ComposeMetalShardCluster(Vector3 basePos, int seed)
{
    static CrystalMaterial s_metalMat;
    static bool s_metalMatLoaded = false;
    if (!s_metalMatLoaded)
    {
        CrystalMaterialParams p = {0};
        p.baseColor = (Color){108, 120, 130, 255}; // cool steel gray, opaque
        p.edgeColor = (Color){245, 250, 255, 255}; // bright silver-white rim
        p.roughness = 0.82f;                       // sharper fresnel edge than ice's 0.35
        p.fresnel = 2.0f;                          // stronger rim-light pop when viewed edge-on
        p.refraction = 0.0f;                       // opaque metal — no fake-glass refraction
        p.sparkle = 1.0f;                          // strong specular glints (steel catch-light)
        p.crack = 0.0f;                            // no internal fracture pattern (that's ice's trait)
        p.emission = 0.06f;
        p.thickness = 0.6f;
        s_metalMat = CrystalMaterial_Load(p);
        s_metalMatLoaded = true;
    }

    // Primary shape — main blades. Drawn one by one (not the uniform cluster
    // helper) so every shard gets its own height/lean/scale: a natural
    // eruption, not four copies of the same spike.
    CrystalDesc desc = {0};
    desc.height = 1.1f;
    desc.radius = 0.11f;
    desc.taper = 0.95f; // near-sharp tip — blade-like, sharper than ice's 0.85
    desc.twist = 0.15f; // minimal twist — rigid metal, not organic ice growth
    desc.noise = 0.03f; // low — faceted-clean rather than jagged
    desc.sides = 5;
    desc.segments = 4;

    CrystalMaterial_Begin(s_metalMat);
    unsigned int rng = (unsigned int)seed * 747796405u + 2891336453u;
    Vector3 tallestTip = basePos;
    float tallestH = 0.0f;
    for (int i = 0; i < 4; i++)
    {
        // Deterministic per-shard variation from the seed (stable across
        // frames — this function draws every frame).
        rng = rng * 1664525u + 1013904223u;
        float r01 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r02 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r03 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;

        float a = (float)i / 4.0f * 2.0f * PI + r01 * 1.2f;
        float dist = 0.06f + r02 * 0.14f;
        Vector3 p = {basePos.x + cosf(a) * dist, basePos.y, basePos.z + sinf(a) * dist};

        CrystalDesc d = desc;
        d.height = desc.height * (0.55f + r03 * 0.7f); // 0.6x..1.25x height spread
        d.radius = desc.radius * (0.75f + r01 * 0.5f);
        d.twist = desc.twist * (0.5f + r02);

        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(r02 * 360.0f, 0, 1, 0);         // random facing
        rlRotatef((r01 - 0.5f) * 24.0f, 1, 0, 0); // slight outward lean
        rlRotatef((r03 - 0.5f) * 24.0f, 0, 0, 1);
        ProceduralMesh_DrawCrystal((Vector3){0, 0, 0}, &d, 1.0f, WHITE);
        rlPopMatrix();

        if (d.height > tallestH)
        {
            tallestH = d.height;
            tallestTip = (Vector3){p.x, p.y + d.height, p.z};
        }
    }

    // Ambient detail — micro crystals scattered at the base: tiny stubs that
    // fill the silhouette's foot so the big blades don't grow out of bare flat
    // ground.
    CrystalDesc micro = desc;
    micro.height = 0.18f;
    micro.radius = 0.045f;
    micro.noise = 0.06f;
    ProceduralMesh_DrawCrystalCluster(basePos, &micro, 5, seed * 7 + 13, 1.0f, WHITE);
    CrystalMaterial_End();

    // Ground crack — the floor shattered where the metal punched through.
    // Gated with a matching lifetime so per-frame callers keep one decal
    // alive instead of stacking hundreds.
    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D crackTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
        DecalSystem_Add(basePos, (float)(seed % 360), 0.9f, crackTex, 1.2f, (Color){200, 210, 220, 200});
    }

    // Highlight — a specular catch-glint winking off the tallest blade's tip.
    if (GetRandomValue(0, 100) < 7)
        VFX_ComposeGlintBurst(tallestTip, 2, 0.05f, VFX_Material(VC_MAT_METAL)->soft);
}

void VFX_ComposeBladeRing(Vector3 pos, float radius, int bladeCount, float rotationDeg)
{
    static EffectMaterial s_bladeMat;
    static bool s_bladeMatLoaded = false;
    if (!s_bladeMatLoaded)
    {
        s_bladeMat = Material_Get(MAT_METAL);
        s_bladeMatLoaded = true;
    }

    // Alive, not mechanical: the ring breathes (±3% radius), wobbles a hair
    // off-plane, and drags a ghost of itself behind the spin.
    float t = (float)GetTime();
    float breathe = 1.0f + 0.03f * sinf(t * 4.0f + pos.x);
    float wobble = 1.5f * sinf(t * 2.3f + pos.z);
    float liveRadius = radius * breathe;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(wobble, 1.0f, 0.0f, 0.0f); // tiny plane tilt — never a dead-flat disc
    rlRotatef(wobble * 0.7f, 0.0f, 0.0f, 1.0f);

    // Primary shape — hub + raked teeth in solid metal.
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
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f); // tip points outward from the ring center
        rlRotatef(12.0f, 1.0f, 0.0f, 0.0f); // slight rake, like angled saw teeth
        DrawCoreCone((Vector3){0, 0, 0}, 0.05f, liveRadius * 0.5f, 6, WHITE);
        rlPopMatrix();
    }
    Material_End();

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    // Secondary motion — spin trail: a ghost pass of the teeth lagging a few
    // degrees behind, faint additive. Sells rotational speed even in a still.
    EffectMaterialParams ghostParams = {0};
    ghostParams.baseColor = (Color){170, 215, 255, 70};
    ghostParams.emissiveIntensity = 1.2f;
    ghostParams.translucency = 0.3f;
    EffectMaterial ghostMat = Material_LoadCustom(ghostParams);
    Material_Begin(ghostMat);
    for (int i = 0; i < bladeCount; i++)
    {
        float a = ((float)i / (float)bladeCount) * 360.0f + rotationDeg - 9.0f; // lag behind the spin
        rlPushMatrix();
        rlRotatef(a, 0.0f, 1.0f, 0.0f);
        rlTranslatef(liveRadius * 0.78f, 0.0f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        rlRotatef(12.0f, 1.0f, 0.0f, 0.0f);
        DrawCoreCone((Vector3){0, 0, 0}, 0.05f, liveRadius * 0.5f, 6, WHITE);
        rlPopMatrix();
    }
    Material_End();

    // Highlight — faint edge glow ring tracing the blade tips.
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

    // Center energy — a small pulsing core the ring spins around, so the
    // formation has a power source instead of orbiting nothing.
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

    // Occasional glint at a random tooth tip — catch-light off spinning steel.
    if (GetRandomValue(0, 100) < 12)
    {
        float ga = ((float)GetRandomValue(0, bladeCount - 1) / (float)bladeCount) * 360.0f + rotationDeg;
        Vector3 tip = {pos.x + cosf(ga * DEG2RAD) * liveRadius * 1.05f, pos.y,
                       pos.z - sinf(ga * DEG2RAD) * liveRadius * 1.05f};
        VFX_ComposeGlintBurst(tip, 2, 0.04f, VFX_Material(VC_MAT_METAL)->soft);
    }
}

// --- Metal skill set ---------------------------------------------------------
// Metal's read is SHARP + SPECULAR: hard fast motion, hot white catch-lights,
// desaturated steel body color. Three pieces skills keep needing:
//   BladeStorm    — continuous orbiting blades around a caster (defense/aura)
//   ShrapnelBurst — one-shot fragment explosion (impact/detonation)
//   RicochetSpark — one-shot directional spark fan (parry/deflect/bullet hit)

static ColorGradient s_steelGrad = {0};  // fragment body: steel gray → dark fade
static ColorGradient s_steelHotGrad = {0}; // white-hot spark tips
static bool s_metalFxInit = false;

static void MetalFx_InitShared(void)
{
    if (s_metalFxInit)
        return;
    ColorGradient_AddStop(&s_steelGrad, 0.0f, (Color){210, 225, 235, 255});
    ColorGradient_AddStop(&s_steelGrad, 0.45f, (Color){149, 165, 166, 200});
    ColorGradient_AddStop(&s_steelGrad, 1.0f, (Color){60, 70, 75, 0});

    ColorGradient_AddStop(&s_steelHotGrad, 0.0f, (Color){255, 255, 250, 255});
    ColorGradient_AddStop(&s_steelHotGrad, 0.3f, (Color){230, 245, 255, 230});
    ColorGradient_AddStop(&s_steelHotGrad, 1.0f, (Color){120, 150, 170, 0});
    s_metalFxInit = true;
}

void VFX_ComposeBladeStorm(Vector3 pos, float radius, float time)
{
    MetalFx_InitShared();

    // ── Primary shape: 7 blades on 2 counter-rotating orbit bands, each with
    // its own radius/height/tilt from a per-blade hash (12.3 — never a
    // stamped-out ring of identical knives).
    static EffectMaterial s_stormMat;
    static bool s_stormMatLoaded = false;
    if (!s_stormMatLoaded)
    {
        s_stormMat = Material_Get(MAT_METAL);
        s_stormMatLoaded = true;
    }

    rlDrawRenderBatchActive();
    Material_Begin(s_stormMat);
    const int bladeCount = 7;
    Vector3 tips[7];
    for (int i = 0; i < bladeCount; i++)
    {
        unsigned int h = (unsigned int)i * 2654435761u;
        float r01 = (float)(h >> 8 & 0xFFFF) / 65535.0f;
        float r02 = (float)(h >> 16 & 0xFFFF) / 65535.0f;

        // Band 0 spins one way, band 1 the other, slightly different speeds.
        int band = i % 2;
        float spin = (band == 0 ? 2.6f : -3.4f) * (0.85f + 0.3f * r01);
        float a = time * spin + (float)i * (2.0f * PI / (float)bladeCount);
        float orbR = radius * (0.75f + 0.35f * r01);
        // Height bobs on its own phase — the storm has volume, not a flat disc.
        float y = radius * (0.35f + 0.4f * r02) + radius * 0.12f * sinf(time * 2.3f + (float)i * 1.7f);

        Vector3 p = {pos.x + cosf(a) * orbR, pos.y + y, pos.z + sinf(a) * orbR};
        tips[i] = p;

        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        // Blade lies along its direction of travel (tangent), with a
        // per-blade rake so edges catch light at different angles.
        rlRotatef(-a * RAD2DEG, 0, 1, 0);                 // face tangent
        rlRotatef(90.0f * (band == 0 ? 1.0f : -1.0f), 1, 0, 0); // point along travel
        rlRotatef((r02 - 0.5f) * 40.0f, 0, 0, 1);         // rake
        DrawCoreCone((Vector3){0, -radius * 0.11f, 0}, radius * 0.035f, radius * 0.22f, 5, WHITE);
        rlPopMatrix();
    }
    Material_End();
    rlDrawRenderBatchActive();

    // ── Secondary motion: silver speed-streaks shed off blade tips — a short
    // tangential particle sells rotation speed far better than the mesh alone.
    if (GetRandomValue(0, 100) < 60)
    {
        int i = GetRandomValue(0, bladeCount - 1);
        Vector3 toCenter = Vector3Subtract(pos, tips[i]);
        Vector3 tangent = Vector3Normalize((Vector3){-toCenter.z, 0.0f, toCenter.x});
        if (i % 2 == 1)
            tangent = Vector3Scale(tangent, -1.0f);
        SpawnParticle((ParticleConfig){
            .position = tips[i],
            .velocity = Vector3Scale(tangent, 1.2f + Random01() * 0.6f),
            .radius = 0.010f + Random01() * 0.006f,
            .lifetime = 0.12f + Random01() * 0.08f,
            .gradient = &s_steelGrad});
    }

    // ── Highlight: catch-light winking off a random blade.
    if (GetRandomValue(0, 100) < 10)
        VFX_ComposeGlintBurst(tips[GetRandomValue(0, bladeCount - 1)], 2, 0.04f,
                              VFX_Material(VC_MAT_METAL)->soft);

    // ── Ambience: cold steel light, faint and flickering.
    if (GetRandomValue(0, 100) < 15)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.5f, 0}),
                       VFX_Material(VC_MAT_METAL)->soft, radius * 2.0f, 0.15f, VFX_PRIORITY_LOW);
}

void VFX_ComposeShrapnelBurst(Vector3 pos, float scale)
{
    MetalFx_InitShared();

    // Heavy fragments: real gravity — steel drops, it doesn't float.
    static ForceField s_fragFld = {0};
    if (s_fragFld.layerCount == 0)
        ForceField_AddLayer(&s_fragFld, (ForceLayer){
                                            .type = FORCE_GRAVITY_DIR,
                                            .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                            .strength = 7.0f});

    // Tail for the streaking fragments (shape from the tail, not the sprite).
    static ParticleConfig s_fragTail;
    s_fragTail = (ParticleConfig){
        .radius = 0.006f * scale,
        .lifetime = 0.12f,
        .gradient = &s_steelGrad};

    // ① Fragment fan — fast, flat-biased (real shrapnel flies outward more
    // than up), three tiers: streaking heroes / body / slow stubs.
    int fragCount = 16 + GetRandomValue(0, 8);
    for (int i = 0; i < fragCount; i++)
    {
        float yaw = Random01() * 2.0f * PI;
        float pitch = (Random01() * 50.0f - 5.0f) * DEG2RAD; // -5°..45°, ground-hugging fan
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        int roll = GetRandomValue(0, 99);
        if (roll < 25)
        {
            SpawnParticle((ParticleConfig){ // hero streak
                .position = pos,
                .velocity = Vector3Scale(dir, (2.8f + Random01() * 1.4f) * scale),
                .radius = 0.013f * scale,
                .lifetime = 0.35f + Random01() * 0.2f,
                .gradient = &s_steelHotGrad,
                .forceField = &s_fragFld,
                .onLiveEmit = &s_fragTail,
                .onLiveEmitRate = 100.0f});
        }
        else
        {
            SpawnParticle((ParticleConfig){ // body fragment
                .position = pos,
                .velocity = Vector3Scale(dir, (1.2f + Random01() * 1.2f) * scale),
                .radius = (0.008f + Random01() * 0.007f) * scale,
                .lifetime = 0.3f + Random01() * 0.25f,
                .gradient = roll < 60 ? &s_steelGrad : &s_steelHotGrad,
                .forceField = &s_fragFld});
        }
    }

    // ② Hot flash + sparkle at the detonation point.
    VFX_ComposeStreakFlare(pos, 0.9f * scale, VFX_Material(VC_MAT_METAL)->soft);
    VFX_ComposeGlintBurst(pos, 10, 0.25f * scale, VFX_Material(VC_MAT_METAL)->soft);

    // ③ Ground scar — impact crater stamp (only when the burst is near the
    // ground; an airburst leaves no crater).
    if (pos.y < 0.6f * scale)
    {
        Texture2D craterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
        DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, (float)GetRandomValue(0, 360),
                        0.55f * scale, craterTex, 2.5f, ColorAlpha(ELEMENT_COLOR_METAL, 0.8f));
    }

    // ④ Pressure pop — small distortion + cold light punch.
    ScreenDistort_Add(pos, 0.6f * scale, 0.22f, 0.25f, 2.5f);
    VFXLight_Spawn(pos, VFX_Material(VC_MAT_METAL)->soft, 2.2f * scale, 0.2f, VFX_PRIORITY_LOW);
}

void VFX_ComposeRicochetSpark(Vector3 pos, Vector3 dir, float scale)
{
    MetalFx_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){0.0f, 1.0f, 0.0f};

    // Basis perpendicular to the deflect direction for the cone spread.
    Vector3 up = fabsf(n.y) > 0.9f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Vector3 side = Vector3Normalize(Vector3CrossProduct(n, up));
    Vector3 side2 = Vector3CrossProduct(n, side);

    static ForceField s_sparkFld = {0};
    if (s_sparkFld.layerCount == 0)
        ForceField_AddLayer(&s_sparkFld, (ForceLayer){
                                             .type = FORCE_GRAVITY_DIR,
                                             .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                             .strength = 4.0f});

    // ① Spark fan — tight ~35° cone around `dir`, the classic blade-on-blade
    // spray. Short and violent: everything dies within a third of a second.
    int sparkCount = 8 + GetRandomValue(0, 4);
    for (int i = 0; i < sparkCount; i++)
    {
        float spread = 0.6f * Random01();
        float ring = Random01() * 2.0f * PI;
        Vector3 d = Vector3Normalize(Vector3Add(n,
                        Vector3Add(Vector3Scale(side, cosf(ring) * spread),
                                   Vector3Scale(side2, sinf(ring) * spread))));
        SpawnParticle((ParticleConfig){
            .position = pos,
            .velocity = Vector3Scale(d, (1.8f + Random01() * 1.5f) * scale),
            .radius = (0.007f + Random01() * 0.006f) * scale,
            .lifetime = 0.12f + Random01() * 0.15f,
            .gradient = &s_steelHotGrad,
            .forceField = &s_sparkFld});
    }

    // ② The "ping" — one micro white flash at contact, no lingering glow.
    SpawnParticle((ParticleConfig){
        .position = pos,
        .colorStart = WHITE,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.05f * scale,
        .lifetime = 0.05f});

    VFXLight_Spawn(pos, VFX_Material(VC_MAT_METAL)->soft, 0.9f * scale, 0.1f, VFX_PRIORITY_LOW);
}
