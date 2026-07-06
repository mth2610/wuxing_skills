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
        VFX_ComposeGlintBurst(tallestTip, 2, 0.05f, (Color){235, 245, 255, 255});
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
        VFX_ComposeGlintBurst(tip, 2, 0.04f, (Color){220, 240, 255, 255});
    }
}
