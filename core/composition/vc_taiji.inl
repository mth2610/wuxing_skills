// Taiji element set — wind, storm, static electricity, yin-yang. Taiji's
// read is DUALITY + MOTION: pale wind that only exists as movement, violet
// lightning, and the white/dark pair orbiting each other. Four pieces:
//   GustSlash    — one-shot directional wind blade (dash, wind cutter)
//   Cyclone      — continuous rising wind column (tornado, mobility ult)
//   StaticField  — continuous crackling micro-arcs in a volume (charge state)
//   YinYangOrbit — continuous white/dark orb pair chasing on a ring (identity)

static ColorGradient s_windGrad = {0};     // pale teal-white, mostly transparent
static ColorGradient s_windDustGrad = {0}; // kicked-up gray dust
static ColorGradient s_yinGrad = {0};      // dark violet trail (dark orb)
static ColorGradient s_yangGrad = {0};     // bright white trail (light orb)
static bool s_taijiFxInit = false;

static void TaijiFx_InitShared(void)
{
    if (s_taijiFxInit)
        return;
    // Wind is visible only where it moves something — keep alpha low, rely
    // on speed + count for the read.
    ColorGradient_AddStop(&s_windGrad, 0.0f, (Color){225, 245, 250, 0});
    ColorGradient_AddStop(&s_windGrad, 0.2f, (Color){210, 240, 245, 140});
    ColorGradient_AddStop(&s_windGrad, 1.0f, (Color){170, 210, 225, 0});

    ColorGradient_AddStop(&s_windDustGrad, 0.0f, (Color){120, 115, 105, 0});
    ColorGradient_AddStop(&s_windDustGrad, 0.3f, (Color){105, 100, 92, 110});
    ColorGradient_AddStop(&s_windDustGrad, 1.0f, (Color){60, 58, 55, 0});

    ColorGradient_AddStop(&s_yinGrad, 0.0f, (Color){90, 40, 140, 200});
    ColorGradient_AddStop(&s_yinGrad, 1.0f, (Color){25, 10, 45, 0});

    ColorGradient_AddStop(&s_yangGrad, 0.0f, (Color){255, 255, 250, 230});
    ColorGradient_AddStop(&s_yangGrad, 1.0f, (Color){150, 130, 200, 0});
    s_taijiFxInit = true;
}

// Deterministic direction on the unit sphere (algebraic form, no acosf) —
// local twin of vc_plasma's helper, distinct name because every .inl shares
// visual_composer.c's translation unit.
static Vector3 TaijiSphereDir(int index, int epoch)
{
    unsigned int rng = (unsigned int)(index * 668265263 + epoch * 374761393) + 1013904223u;
    rng ^= rng >> 13;
    rng *= 1274126177u;
    rng ^= rng >> 16;
    float u = (float)(rng & 0xFFFF) / 65535.0f;
    rng = rng * 1664525u + 1013904223u;
    float v = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
    float yaw = u * 2.0f * PI;
    float t = 2.0f * v - 1.0f;
    float rxy = sqrtf(fmaxf(1.0f - t * t, 0.0f));
    return (Vector3){rxy * cosf(yaw), t, rxy * sinf(yaw)};
}

void VFX_ComposeGustSlash(Vector3 pos, Vector3 dir, float scale)
{
    TaijiFx_InitShared();

    Vector3 n = Vector3Normalize(dir);
    if (Vector3Length(dir) < 0.001f)
        n = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 side = Vector3Normalize((Vector3){-n.z, 0.0f, n.x});

    // ① Crescent fan — streaks launched along `dir` spread across a flat
    // ~80° arc (a blade, not a cone): outer streaks angle away, all race
    // forward and die within a quarter second.
    int streakCount = 10 + GetRandomValue(0, 4);
    for (int i = 0; i < streakCount; i++)
    {
        float f = ((float)i / (float)(streakCount - 1)) * 2.0f - 1.0f; // -1..1 across the blade
        Vector3 d = Vector3Normalize(Vector3Add(n, Vector3Scale(side, f * 0.7f)));
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(side, f * 0.25f * scale)),
            .velocity = Vector3Scale(d, (3.0f + Random01() * 1.5f) * scale),
            .radius = (0.010f + Random01() * 0.008f) * scale,
            .lifetime = 0.18f + Random01() * 0.1f,
            .gradient = &s_windGrad});
    }

    // ② Dust wake — ground dirt kicked sideways out of the blade's path.
    for (int i = 0; i < 5; i++)
    {
        float f = Random01() * 2.0f - 1.0f;
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(side, f * 0.3f * scale)),
            .velocity = Vector3Add(Vector3Scale(n, 0.6f), Vector3Scale(side, f * 0.9f)),
            .radius = (0.03f + Random01() * 0.025f) * scale,
            .lifetime = 0.4f + Random01() * 0.3f,
            .gradient = &s_windDustGrad});
    }

    // ③ Ground groove — the slash mark the wind leaves, oriented along dir.
    Texture2D grooveTex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
    float yawDeg = atan2f(-n.z, n.x) * RAD2DEG;
    DecalSystem_Add((Vector3){pos.x, 0.0f, pos.z}, yawDeg, 0.6f * scale,
                    grooveTex, 0.8f, ColorAlpha((Color){210, 235, 240, 255}, 0.55f));

    // ④ Air rip — brief distortion smear along the path; wind has no color,
    // the bent air IS the effect.
    ScreenDistort_Add(Vector3Add(pos, Vector3Scale(n, 0.4f * scale)),
                      0.5f * scale, 0.15f, 0.2f, 3.0f);
}

void VFX_ComposeCyclone(Vector3 pos, float radius, float time)
{
    TaijiFx_InitShared();

    float height = radius * 2.4f;

    // Vortex + updraft + inward pull (keeps the cone tight), origin tracks pos.
    static ForceField s_cycFld;
    ForceField_Clear(&s_cycFld);
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_VORTEX,
                                       .origin = pos,
                                       .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                       .strength = 6.0f,
                                       .radius = radius * 3.0f,
                                       .falloff = 1.0f});
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_GRAVITY_DIR,
                                       .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                       .strength = 2.2f});
    ForceField_AddLayer(&s_cycFld, (ForceLayer){
                                       .type = FORCE_GRAVITY_POINT,
                                       .origin = Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                                       .strength = 1.8f,
                                       .radius = radius * 3.0f,
                                       .falloff = 1.0f});

    // ── Wind body: streaks fed in at the base rim by a rotating spawn arm
    // (consecutive frames ignite neighbouring angles → visible spiral bands
    // climbing the funnel, same trick as FirePillar's arm).
    int bodyCount = 3;
    for (int i = 0; i < bodyCount; i++)
    {
        float armA = time * 7.0f + (float)i * (2.0f * PI / (float)bodyCount);
        float rr = radius * (0.5f + 0.3f * Random01());
        Vector3 spawn = {pos.x + cosf(armA) * rr, pos.y + Random01() * height * 0.15f,
                         pos.z + sinf(armA) * rr};
        Vector3 tangent = {-sinf(armA), 0.3f, cosf(armA)};
        SpawnParticle((ParticleConfig){
            .position = spawn,
            .velocity = Vector3Scale(tangent, 1.2f),
            .radius = 0.014f + Random01() * 0.010f,
            .lifetime = 0.7f + Random01() * 0.5f,
            .gradient = &s_windGrad,
            .forceField = &s_cycFld});
    }

    // ── Dust skirt: heavier gray puffs flung around the base — anchors the
    // funnel to the ground.
    if (GetRandomValue(0, 100) < 55)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.9f, pos.y + 0.03f,
                                  pos.z + sinf(a) * radius * 0.9f},
            .velocity = (Vector3){-sinf(a) * 0.8f, 0.1f, cosf(a) * 0.8f},
            .radius = 0.035f + Random01() * 0.03f,
            .lifetime = 0.5f + Random01() * 0.4f,
            .gradient = &s_windDustGrad,
            .forceField = &s_cycFld});
    }

    // ── Debris flecks: rare darker chips riding the column high — scale cue.
    if (GetRandomValue(0, 100) < 12)
    {
        float a = Random01() * 2.0f * PI;
        SpawnParticle((ParticleConfig){
            .position = (Vector3){pos.x + cosf(a) * radius * 0.4f,
                                  pos.y + Random01() * height * 0.5f,
                                  pos.z + sinf(a) * radius * 0.4f},
            .velocity = (Vector3){-sinf(a) * 1.5f, 0.8f, cosf(a) * 1.5f},
            .radius = 0.009f + Random01() * 0.006f,
            .lifetime = 0.8f + Random01() * 0.5f,
            .gradient = &s_windDustGrad,
            .forceField = &s_cycFld});
    }

    // ── The funnel bends air: slow tall distortion column, gated.
    if (GetRandomValue(0, 100) < 8)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, height * 0.5f, 0}),
                          radius * 1.6f, 0.09f, 0.7f, 1.2f);
}
