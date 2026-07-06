void VFX_ComposeStonePillar(Vector3 basePos, float progress)
{
    if (progress <= 0.0f)
        return;

    rlDisableBackfaceCulling();
    float height = 1.8f;
    float radius = 0.35f;

    float rise = progress * progress * (3.0f - 2.0f * progress);
    float yOffset = -height * (1.0f - rise) - 0.05f;
    Vector3 actualPos = Vector3Add(basePos, (Vector3){0, yOffset, 0});
    float topRadius = radius * 0.55f;

    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    ProceduralMesh_DrawOrganicStonePillar(actualPos, height + 0.05f, radius, topRadius);
    Material_End();
}

void VFX_ComposeBoulder(Vector3 pos)
{
    float randScale = 0.88f + ((float)rand() / (float)RAND_MAX * 0.24f);

    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    DrawCoreSphere(pos, 0.22f * randScale, 24, 24, WHITE);
    Material_End();

    RockMeshData *data = MeshCache_GetRock(rand() % 5000, 0.45f);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Material_Begin(mat);

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);

    rlRotatef((float)(rand() % 360), 0.0f, 1.0f, 0.0f);
    rlRotatef((float)(rand() % 20 - 10), 1.0f, 0.0f, 0.0f);

    rlScalef(0.25f * randScale, 0.25f * randScale, 0.25f * randScale);
    ProceduralMesh_DrawRock(data, WHITE);
    rlPopMatrix();

    Material_End();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width)
{
    float totalDist = Vector3Distance(start, end);
    if (totalDist > 0.001f)
    {
        Vector3 dir = Vector3Normalize(Vector3Subtract(end, start));
        Vector3 right = (Vector3){-dir.z, 0.0f, dir.x};
        float halfW = width * 0.5f;

        Vector3 p1 = Vector3Add(start, Vector3Scale(right, -halfW));
        Vector3 p2 = Vector3Add(end, Vector3Scale(right, -halfW));
        Vector3 p3 = Vector3Add(end, Vector3Scale(right, halfW));
        Vector3 p4 = Vector3Add(start, Vector3Scale(right, halfW));

        float yOffset = 0.006f;
        p1.y += yOffset;
        p2.y += yOffset;
        p3.y += yOffset;
        p4.y += yOffset;

        Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");

        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ALPHA);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(p3.x, p3.y, p3.z);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(p4.x, p4.y, p4.z);
        rlEnd();
        rlSetTexture(0);

        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }
}

// --- Earth skill set ---------------------------------------------------------
// Earth's read is MASS: nothing floats unless magic holds it up, everything
// that breaks falls HARD, and the tell of impact is dust, not glow (earth is
// the least emissive element — light stays faint and warm). Three pieces:
//   RockBurst      — one-shot debris explosion (impact, shatter)
//   FloatingStones — continuous levitating rocks around a caster (buff/charge)
//   QuakeRumble    — continuous trembling zone (pebbles hop, dust pops)

static ColorGradient s_earthDustGrad = {0};   // ochre-gray airborne dust
static ColorGradient s_earthChunkGrad = {0};  // rock chip: lit brown → dark
static ColorGradient s_earthGrainGrad = {0};  // bright sand grains (catchlight)
static SkillCurve s_earthDustBillow = {0};    // dust grows while fading
static bool s_earthFxInit = false;

static void EarthFx_InitShared(void)
{
    if (s_earthFxInit)
        return;
    ColorGradient_AddStop(&s_earthDustGrad, 0.0f, (Color){150, 125, 95, 0});
    ColorGradient_AddStop(&s_earthDustGrad, 0.25f, (Color){140, 115, 88, 120});
    ColorGradient_AddStop(&s_earthDustGrad, 1.0f, (Color){85, 72, 60, 0});

    ColorGradient_AddStop(&s_earthChunkGrad, 0.0f, (Color){175, 130, 85, 255});
    ColorGradient_AddStop(&s_earthChunkGrad, 0.6f, (Color){120, 90, 60, 220});
    ColorGradient_AddStop(&s_earthChunkGrad, 1.0f, (Color){55, 42, 32, 0});

    ColorGradient_AddStop(&s_earthGrainGrad, 0.0f, (Color){235, 200, 150, 255});
    ColorGradient_AddStop(&s_earthGrainGrad, 0.5f, (Color){190, 150, 100, 200});
    ColorGradient_AddStop(&s_earthGrainGrad, 1.0f, (Color){100, 80, 55, 0});

    FloatCurve_AddStop(&s_earthDustBillow, 0.0f, 0.45f);
    FloatCurve_AddStop(&s_earthDustBillow, 1.0f, 1.8f);
    s_earthFxInit = true;
}

// Shared heavy-gravity field — rock falls at real weight, no floatiness.
static ForceField s_earthGravFld = {0};
static ForceField *EarthGravField(void)
{
    if (s_earthGravFld.layerCount == 0)
        ForceField_AddLayer(&s_earthGravFld, (ForceLayer){
                                                 .type = FORCE_GRAVITY_DIR,
                                                 .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                                 .strength = 9.8f});
    return &s_earthGravFld;
}

void VFX_ComposeRockBurst(Vector3 pos, float scale)
{
    EarthFx_InitShared();

    // ① Rock chunks — heavy fragments thrown 20°–70° up, arcing hard back
    // down. A few bright sand-grain heroes catch the light at the top.
    int chunkCount = 14 + GetRandomValue(0, 6);
    for (int i = 0; i < chunkCount; i++)
    {
        float yaw = Random01() * 2.0f * PI;
        float pitch = (20.0f + Random01() * 50.0f) * DEG2RAD;
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        bool grain = GetRandomValue(0, 100) < 25;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, 0.05f * scale)),
            .velocity = Vector3Scale(dir, (1.3f + Random01() * 1.5f) * scale),
            .radius = (grain ? 0.008f : 0.014f) * scale * (0.75f + Random01() * 0.5f),
            .lifetime = 0.5f + Random01() * 0.3f,
            .gradient = grain ? &s_earthGrainGrad : &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    // ② Dust cloud — the real body of an earth impact: slow fat billows
    // swelling outward low to the ground, far outliving the chunks.
    for (int i = 0; i < 8; i++)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * 0.12f * scale, pos.y + 0.04f,
                                  pos.z + sinf(a) * 0.12f * scale},
            .velocity = (Vector3){cosf(a) * (0.4f + Random01() * 0.3f),
                                  0.1f + Random01() * 0.15f,
                                  sinf(a) * (0.4f + Random01() * 0.3f)},
            .radius = (0.05f + Random01() * 0.045f) * scale,
            .lifetime = 0.8f + Random01() * 0.6f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }

    // ③ Shatter scar — stamped crack, long-lived: the ground remembers.
    Texture2D shatterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
    DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, (float)GetRandomValue(0, 360),
                    0.6f * scale, shatterTex, 3.5f, ColorAlpha(WHITE, 0.85f));

    // ④ Thud — earth impact is felt, not seen: shake + dusty distortion,
    // only a faint warm light (dust doesn't glow).
    CameraFX_Shake(0.25f * fminf(scale, 1.5f));
    ScreenDistort_Add(pos, 0.5f * scale, 0.15f, 0.2f, 1.2f);
    VFXLight_Spawn(pos, VFX_Material(VC_MAT_EARTH)->soft, 1.2f * scale, 0.15f, VFX_PRIORITY_LOW);
}

void VFX_ComposeFloatingStones(Vector3 pos, float radius, float time)
{
    EarthFx_InitShared();

    // ── Primary shape: 5 real rock meshes levitating in a loose ring — each
    // gets a stable seed (same rock every frame), own orbit speed, height,
    // bob phase and tumble axis. Slow everything: mass reads through inertia.
    static EffectMaterial s_rockMat;
    static bool s_rockMatLoaded = false;
    if (!s_rockMatLoaded)
    {
        s_rockMat = Material_Get(MAT_ROCK);
        s_rockMatLoaded = true;
    }

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Material_Begin(s_rockMat);
    const int stoneCount = 5;
    Vector3 stonePos[5];
    for (int i = 0; i < stoneCount; i++)
    {
        unsigned int h = (unsigned int)i * 2246822519u + 3266489917u;
        float r01 = (float)(h >> 8 & 0xFFFF) / 65535.0f;
        float r02 = (float)(h >> 20 & 0xFFF) / 4095.0f;

        float a = time * (0.5f + 0.25f * r01) * ((i % 2) ? 1.0f : -1.0f)
                  + (float)i * (2.0f * PI / (float)stoneCount);
        float orbR = radius * (0.75f + 0.4f * r01);
        float y = radius * (0.5f + 0.55f * r02) + radius * 0.1f * sinf(time * 1.3f + (float)i * 2.1f);
        Vector3 p = {pos.x + cosf(a) * orbR, pos.y + y, pos.z + sinf(a) * orbR};
        stonePos[i] = p;

        RockMeshData *rock = MeshCache_GetRock(i * 733 + 17, 0.5f);
        float s = radius * (0.10f + 0.08f * r02);
        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(time * (8.0f + 10.0f * r01), r01, 1.0f, r02); // lazy tumble
        rlScalef(s, s, s);
        ProceduralMesh_DrawRock(rock, WHITE);
        rlPopMatrix();
    }
    Material_End();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();

    // ── Anti-gravity tell: dust motes rising UNDER the stones — the magic
    // visibly pulling upward is what says "levitation", not the stones alone.
    if (GetRandomValue(0, 100) < 40)
    {
        int i = GetRandomValue(0, stoneCount - 1);
        SpawnParticle((ParticleConfig){
            .position = (Vector3){stonePos[i].x + (Random01() - 0.5f) * 0.08f,
                                  pos.y + 0.03f,
                                  stonePos[i].z + (Random01() - 0.5f) * 0.08f},
            .velocity = (Vector3){0, 0.25f + Random01() * 0.2f, 0},
            .radius = 0.006f + Random01() * 0.005f,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_earthGrainGrad});
    }

    // ── Mass tell: a pebble occasionally shakes loose and falls hard.
    if (GetRandomValue(0, 100) < 8)
    {
        int i = GetRandomValue(0, stoneCount - 1);
        SpawnParticle((ParticleConfig){
            .position = stonePos[i],
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, -0.1f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.007f + Random01() * 0.004f,
            .lifetime = 0.6f,
            .gradient = &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    // Warm faint light — enchanted, not burning.
    if (GetRandomValue(0, 100) < 10)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.6f, 0}),
                       VFX_Material(VC_MAT_EARTH)->soft, radius * 1.8f, 0.25f, VFX_PRIORITY_LOW);
}

void VFX_ComposeQuakeRumble(Vector3 pos, float radius, float time)
{
    EarthFx_InitShared();

    // ── Hopping pebbles — the classic quake tell: grit thrown a hand-span
    // up off the shaking ground, falling right back.
    int hops = 1 + GetRandomValue(0, 1);
    for (int i = 0; i < hops; i++)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * sqrtf(Random01());
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.02f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.15f,
                                  0.6f + Random01() * 0.6f,
                                  (Random01() - 0.5f) * 0.15f},
            .radius = 0.006f + Random01() * 0.006f,
            .lifetime = 0.35f + Random01() * 0.15f,
            .gradient = GetRandomValue(0, 100) < 30 ? &s_earthGrainGrad : &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    // ── Dust pops — puffs venting from the ground at random points.
    if (GetRandomValue(0, 100) < 35)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * Random01();
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * rr, pos.y + 0.03f, pos.z + sinf(a) * rr},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, 0.15f + Random01() * 0.1f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.04f + Random01() * 0.03f,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }

    // ── Spreading damage: new cracks stamped occasionally while it rumbles.
    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D shatterTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
        float a = Random01() * 2.0f * PI;
        float rr = radius * 0.7f * Random01();
        DecalSystem_Add((Vector3){pos.x + cosf(a) * rr, 0.0f, pos.z + sinf(a) * rr},
                        (float)GetRandomValue(0, 360), 0.35f + Random01() * 0.25f,
                        shatterTex, 2.5f, ColorAlpha(WHITE, 0.7f));
    }

    // ── The rumble itself — continuous low-grade shake while the zone lives.
    if (GetRandomValue(0, 100) < 25)
        CameraFX_Shake(0.05f);

    (void)time;
}
