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

// --- Earth skill set ---------------------------------------------------------
// Earth's read is MASS: nothing floats unless magic holds it up, everything
// that breaks falls HARD, and the tell of impact is dust, not glow (earth is
// the least emissive element — light stays faint and warm). Three pieces:
//   RockBurst      — one-shot debris explosion (impact, shatter)
//   FloatingStones — continuous levitating rocks around a caster (buff/charge)
//   QuakeRumble    — continuous trembling zone (pebbles hop, dust pops)

static ColorGradient s_earthDustGrad = {0};  // ochre-gray airborne dust
static ColorGradient s_earthChunkGrad = {0}; // rock chip: lit brown → dark
static ColorGradient s_earthGrainGrad = {0}; // bright sand grains (catchlight)
static SkillCurve s_earthDustBillow = {0};   // dust grows while fading
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

// FissureStreak — ground crack running from `start` to `end`, real 3D
// V-groove geometry (not a flat decal quad) via ProceduralMesh_BuildFissure's
// midpoint-displacement-style noise (lateral centerline jitter + edge/shoulder/
// bottom jitter, seeded so the shape is stable per start/end pair, not
// re-rolled every frame). `progress` (0..1) reveals segments A→B as the
// crack tears open — driven by the caster's cast-time or a skill's travel
// timer, NOT re-randomized. `time` only drives the ember-seam pulse below.
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time)
{
    float totalDist = Vector3Distance(start, end);
    if (totalDist < 0.05f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f)
        return;

    int seed = (int)(start.x * 131.0f) + (int)(start.z * 977.0f) +
               (int)(end.x * 53.0f) + (int)(end.z * 197.0f);

    // Small nominal lift — kept the crack near the sampled ground level so
    // it doesn't visibly hover, but no longer needs to out-margin the
    // real ground mesh's depth: the structural draw below disables depth
    // TEST entirely against it instead (see that comment). Chasing a yLift
    // large enough to clear every noise-jittered shoulder/floor vertex
    // (0.03 -> 0.06 -> 0.09, still partially occluded each time) was
    // fighting the wrong problem — a ground-hugging "cut into solid,
    // already-rendered opaque terrain" effect fundamentally can't rely on
    // real depth occlusion to look right unless the ground mesh actually
    // has a hole there, which it doesn't.
    Vector3 liftedStart = Vector3Add(start, (Vector3){0.0f, 0.02f, 0.0f});
    Vector3 liftedEnd   = Vector3Add(end,   (Vector3){0.0f, 0.02f, 0.0f});
    Vector3 path[2] = {liftedStart, liftedEnd};
    static FissureMeshData mesh;
    ProceduralMesh_BuildFissure(&mesh, path, 2, width, 0.12f, 0.75f, seed);
    if (mesh.segments < 1)
        return;

    int revealSeg = (int)(mesh.segments * progress + 0.999f);
    if (revealSeg < 1)
        revealSeg = 1;
    if (revealSeg > mesh.segments)
        revealSeg = mesh.segments;

    // ① Structural crack — self-shaded gradient (mép sáng ấm bắt sáng → vai
    // nâu tối → đáy gần đen), KHÔNG dùng EffectMaterial(lit): map/scene ở
    // đây gần như không có ánh sáng thật, nên mesh lit chìm thành đen-trên-
    // đen và vô hình (bug đã thấy: chỉ còn dải ember mỏng hiện ra). Dedicated
    // geometry cho hình dạng này giờ tự mang shading riêng, không phụ thuộc
    // scene lighting — xem ProceduralMesh_DrawFissureShaded.
    Color crossColors[FISSURE_CROSS_VERTS] = {
        (Color){95, 78, 58, 255},  // mép trái — bắt sáng viền
        (Color){52, 42, 32, 255},  // vai trái
        (Color){14, 10, 8, 255},   // đáy — gần đen, sâu nhất
        (Color){52, 42, 32, 255},  // vai phải
        (Color){95, 78, 58, 255},  // mép phải
    };
    // Two-sided: the V-groove's side walls face left/right, not just up, so
    // with backface culling on (the default rlgl state going into this
    // call) whichever wall faces away from the camera drops out entirely —
    // this is why the crack looked like a faint sliver or vanished outright
    // depending on view angle/crack orientation.
    //
    // Depth test OFF (not just depth mask): on a real heightmap map the
    // ground is a genuinely opaque, already-rendered mesh — a "cut into the
    // ground" effect can only ever be a visual approximation on top of it
    // (there's no actual hole in that mesh), so trying to win real depth
    // competition against it means matching its exact surface height
    // everywhere along the crack, including per-segment jaggedness noise —
    // fragile and, in practice, never fully reliable (see the yLift history
    // above). Drawing on top unconditionally guarantees visibility; the
    // V-groove shading (bright rim -> dark floor) still sells the "cut into
    // the ground" look on its own without needing real occlusion.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthTest();
    ProceduralMesh_DrawFissureShaded(&mesh, crossColors, revealSeg);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableBackfaceCulling();

    // ② Ember seam — warm glow pulsing along the crack floor AND rim, wide
    // enough to actually read at gameplay distance (thin 1-line glow was
    // the earlier bug). Earth stays the least-emissive element, so this is
    // still restrained — not lava-bright.
    float pulse = 0.55f + 0.45f * sinf(time * 3.2f);
    unsigned char glowA = (unsigned char)(90.0f * pulse);
    float glowHW = width * 0.55f;

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);
    rlColor4ub(255, 140, 70, glowA);
    for (int i = 0; i < revealSeg; i++)
    {
        Vector3 acrossA = Vector3Normalize(Vector3Subtract(mesh.verts[i][3], mesh.verts[i][1]));
        Vector3 acrossB = Vector3Normalize(Vector3Subtract(mesh.verts[i + 1][3], mesh.verts[i + 1][1]));
        Vector3 a0 = Vector3Add(mesh.verts[i][2], Vector3Scale(acrossA, -glowHW));
        Vector3 a1 = Vector3Add(mesh.verts[i][2], Vector3Scale(acrossA, glowHW));
        Vector3 b1 = Vector3Add(mesh.verts[i + 1][2], Vector3Scale(acrossB, glowHW));
        Vector3 b0 = Vector3Add(mesh.verts[i + 1][2], Vector3Scale(acrossB, -glowHW));
        a0.y += 0.008f;
        a1.y += 0.008f;
        b0.y += 0.008f;
        b1.y += 0.008f;
        rlVertex3f(a0.x, a0.y, a0.z);
        rlVertex3f(a1.x, a1.y, a1.z);
        rlVertex3f(b1.x, b1.y, b1.z);
        rlVertex3f(b0.x, b0.y, b0.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // ③ Leading-edge dust — a little grit kicked up right where the crack is
    // actively tearing open, not smeared along the whole length.
    if (progress < 1.0f && GetRandomValue(0, 100) < 30)
    {
        EarthFx_InitShared();
        Vector3 tipDir = Vector3Normalize(Vector3Subtract(end, start));
        Vector3 tip = Vector3Add(start, Vector3Scale(tipDir, totalDist * progress));
        SpawnParticle((ParticleConfig){
            .position = (Vector3){tip.x, tip.y + 0.03f, tip.z},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.3f, 0.3f + Random01() * 0.3f, (Random01() - 0.5f) * 0.3f},
            .radius = 0.03f + Random01() * 0.02f,
            .lifetime = 0.4f + Random01() * 0.3f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }
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

// GPU-resident rock template (UploadMesh once, same lifetime as a shader —
// never rebuilt) for VFX_ComposeFloatingStones' instanced draw below. See
// ProceduralMesh_BuildRockTemplateMesh's doc comment and CORE_ISSUES.md
// Item 40 for the pattern this mirrors (crystal template + DrawMeshInstanced).
static Mesh GetFloatingStoneTemplateMesh(void)
{
    static Mesh s_template = {0};
    static bool s_ready = false;
    if (!s_ready)
    {
        s_template = ProceduralMesh_BuildRockTemplateMesh(1.0f, 0.5f, 733, 1);
        s_ready = true;
    }
    return s_template;
}

// Instanced-shader twin of Material_Get(MAT_ROCK) — separate shader program
// (effect_material_instanced.vs) needs its own uniform-location cache, see
// EffectMaterialInstanced's doc comment in material_system.h.
static EffectMaterialInstanced GetFloatingStoneMaterialInstanced(void)
{
    static EffectMaterialInstanced s_rockMatI;
    static bool s_rockMatILoaded = false;
    if (!s_rockMatILoaded)
    {
        EffectMaterial nonInstanced = Material_Get(MAT_ROCK);
        s_rockMatI = EffectMaterialInstanced_Load(nonInstanced.params);
        s_rockMatILoaded = true;
    }
    return s_rockMatI;
}

#define FLOATING_STONE_MAX 8 // reasonable stack cap for the transform array, not a real performance limit

void VFX_ComposeFloatingStones(Vector3 pos, float radius, float time)
{
    EarthFx_InitShared();

    // ── Primary shape: 5 rock instances levitating in a loose ring, one
    // DrawMeshInstanced call — each gets its own orbit speed, height, bob
    // phase and tumble axis via transform only (shared rock silhouette,
    // CORE_ISSUES.md Item 40 trade-off). Slow everything: mass reads
    // through inertia.
    Mesh templateMesh = GetFloatingStoneTemplateMesh();
    const int stoneCount = 5;
    Vector3 stonePos[5];
    Matrix transforms[FLOATING_STONE_MAX];

    if (templateMesh.vertexCount > 0)
    {
        for (int i = 0; i < stoneCount; i++)
        {
            unsigned int h = (unsigned int)i * 2246822519u + 3266489917u;
            float r01 = (float)(h >> 8 & 0xFFFF) / 65535.0f;
            float r02 = (float)(h >> 20 & 0xFFF) / 4095.0f;

            float a = time * (0.5f + 0.25f * r01) * ((i % 2) ? 1.0f : -1.0f) + (float)i * (2.0f * PI / (float)stoneCount);
            float orbR = radius * (0.75f + 0.4f * r01);
            float y = radius * (0.5f + 0.55f * r02) + radius * 0.1f * sinf(time * 1.3f + (float)i * 2.1f);
            Vector3 p = {pos.x + cosf(a) * orbR, pos.y + y, pos.z + sinf(a) * orbR};
            stonePos[i] = p;

            float s = radius * (0.10f + 0.08f * r02);
            float tumbleRad = (time * (8.0f + 10.0f * r01)) * DEG2RAD; // lazy tumble
            Matrix scale = MatrixScale(s, s, s);
            Matrix rot = MatrixRotate((Vector3){r01, 1.0f, r02}, tumbleRad);
            Matrix translate = MatrixTranslate(p.x, p.y, p.z);
            transforms[i] = MatrixMultiply(MatrixMultiply(scale, rot), translate);
        }

        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        EffectMaterialInstanced rockMatI = GetFloatingStoneMaterialInstanced();
        EffectMaterialInstanced_Begin(rockMatI);
        Material passthrough = ProceduralMesh_GetPassthroughMaterial(rockMatI.shader);
        DrawMeshInstanced(templateMesh, passthrough, transforms, stoneCount);
        EffectMaterialInstanced_End();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
    else
    {
        for (int i = 0; i < stoneCount; i++) stonePos[i] = pos;
    }

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
