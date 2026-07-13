// Energy Smoke — two shader-driven density fields, no texture atlas, no
// particle system, no external asset dependency (see the flipbook/video
// asset-sourcing detour this replaced).
//
// VFX_ComposeEnergySmoke (core/shaders/energy_smoke.fs) and
// VFX_ComposeSmokeColumnFX (core/shaders/smoke_column.fs) are DELIBERATELY
// separate shaders, not one shader with mode uniforms — a static-point
// "shockwave ring" and a base-anchored "rising column" are different
// physical shapes (rotationally-symmetric-around-a-fixed-point vs an
// advecting blob that travels upward), and one shared shader trying to
// cover both via uniforms was hard to tune without one case regressing the
// other (2026-07-10: this is exactly what happened — riseSpeed/ringStrength
// uniforms bolted onto energy_smoke.fs fixed the column but made both
// harder to reason about). Each shader now only has to get ONE shape right.
//
// Cost model (why SmokePuff/SmokeTrail don't use this but SmokeColumn does):
// this is a per-pixel shader (FBM domain-warp, several noise evaluations per
// pixel) over however many quads × however much screen area they cover.
// SmokePuff/SmokeTrail were reverted to particles because they fire often
// from gameplay — many concurrent small quads stacking up fast. SmokeColumn
// is normally 1-3 crossed quads for a SINGLE long-lived instance (a torch,
// incense, chimney) — few concurrent shader instances, so the per-pixel
// cost is affordable even though each quad's screen footprint is taller.

// ── Energy Smoke puff (energy_smoke.fs) ────────────────────────────────────────

static Shader s_smokeSh = {0};
static int s_uColor = -1;
static int s_uProgress = -1;
static int s_uDiffusion = -1;
static int s_uNoiseScale = -1;
static int s_uDriftSpeed = -1;
static int s_uSourcePos = -1;
static bool s_smokeInit = false;

// Batch and Uniform State Caching for Energy Smoke Puffs
static bool s_smokeBatchActive = false;
static Color s_lastSmokeTint = {0, 0, 0, 0};
static float s_lastSmokeProgress = -1.0f;
static float s_lastSmokeDiffusion = -1.0f;
static float s_lastSmokeNoiseScale = -1.0f;
static float s_lastSmokeDriftSpeed = -1.0f;
static Vector2 s_lastSmokeSourceUV = {-1.0f, -1.0f};

static void SmokeShader_Init(void)
{
    if (s_smokeInit)
        return;
    s_smokeSh = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                           "core/shaders/energy_smoke.fs");
    s_uColor = GetShaderLocation(s_smokeSh, "u_color");
    s_uProgress = GetShaderLocation(s_smokeSh, "u_progress");
    s_uDiffusion = GetShaderLocation(s_smokeSh, "u_diffusion");
    s_uNoiseScale = GetShaderLocation(s_smokeSh, "u_noiseScale");
    s_uDriftSpeed = GetShaderLocation(s_smokeSh, "u_driftSpeed");
    s_uSourcePos = GetShaderLocation(s_smokeSh, "u_sourcePos");
    s_smokeInit = true;
}

void VFX_BeginEnergySmokeBatch(void)
{
    if (s_smokeBatchActive)
        return;
    SmokeShader_Init();
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    s_smokeBatchActive = true;

    // Reset caches on begin
    s_lastSmokeProgress = -1.0f;
    s_lastSmokeDiffusion = -1.0f;
    s_lastSmokeNoiseScale = -1.0f;
    s_lastSmokeDriftSpeed = -1.0f;
    s_lastSmokeSourceUV = (Vector2){-1.0f, -1.0f};
    s_lastSmokeTint = (Color){0, 0, 0, 0};
}

void VFX_EndEnergySmokeBatch(void)
{
    if (!s_smokeBatchActive)
        return;
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
    s_smokeBatchActive = false;
}

static void SmokeShader_SetUniforms(Color tint, float progress, float diffusion,
                                    float noiseScale, float driftSpeed, Vector2 sourceUV)
{
    bool changed = (progress != s_lastSmokeProgress) || 
                   (diffusion != s_lastSmokeDiffusion) ||
                   (noiseScale != s_lastSmokeNoiseScale) ||
                   (driftSpeed != s_lastSmokeDriftSpeed) ||
                   (sourceUV.x != s_lastSmokeSourceUV.x || sourceUV.y != s_lastSmokeSourceUV.y) ||
                   (tint.r != s_lastSmokeTint.r || tint.g != s_lastSmokeTint.g || tint.b != s_lastSmokeTint.b || tint.a != s_lastSmokeTint.a);

    if (changed)
    {
        rlDrawRenderBatchActive(); // Flush before uploading new uniforms

        if (progress != s_lastSmokeProgress)
        {
            if (s_uProgress >= 0)
                SetShaderValue(s_smokeSh, s_uProgress, &progress, SHADER_UNIFORM_FLOAT);
            s_lastSmokeProgress = progress;
        }
        if (diffusion != s_lastSmokeDiffusion)
        {
            if (s_uDiffusion >= 0)
                SetShaderValue(s_smokeSh, s_uDiffusion, &diffusion, SHADER_UNIFORM_FLOAT);
            s_lastSmokeDiffusion = diffusion;
        }
        if (noiseScale != s_lastSmokeNoiseScale)
        {
            if (s_uNoiseScale >= 0)
                SetShaderValue(s_smokeSh, s_uNoiseScale, &noiseScale, SHADER_UNIFORM_FLOAT);
            s_lastSmokeNoiseScale = noiseScale;
        }
        if (driftSpeed != s_lastSmokeDriftSpeed)
        {
            if (s_uDriftSpeed >= 0)
                SetShaderValue(s_smokeSh, s_uDriftSpeed, &driftSpeed, SHADER_UNIFORM_FLOAT);
            s_lastSmokeDriftSpeed = driftSpeed;
        }
        if (sourceUV.x != s_lastSmokeSourceUV.x || sourceUV.y != s_lastSmokeSourceUV.y)
        {
            if (s_uSourcePos >= 0)
                SetShaderValue(s_smokeSh, s_uSourcePos, &sourceUV, SHADER_UNIFORM_VEC2);
            s_lastSmokeSourceUV = sourceUV;
        }
        if (tint.r != s_lastSmokeTint.r || tint.g != s_lastSmokeTint.g || tint.b != s_lastSmokeTint.b || tint.a != s_lastSmokeTint.a)
        {
            Vector4 colorV = ColorNormalize(tint);
            if (s_uColor >= 0)
                SetShaderValue(s_smokeSh, s_uColor, &colorV, SHADER_UNIFORM_VEC4);
            s_lastSmokeTint = tint;
        }
    }
}

// ONE puff that blooms out slowly and hazily then dissolves. Drawn on a
// camera-facing billboard quad (DrawCoreBillboardQuad), not a sphere mesh —
// a sphere's silhouette is always a hard geometric circle in screen space
// regardless of surface shading; a flat quad has no such constraint, so the
// shape falls entirely out of the shader's own alpha. Cheaper too: 1 quad
// (2 triangles) vs a 20x20 sphere (800 tris).
//
// energy_smoke.fs is the closed-form analytic solution to the 2D diffusion
// equation for a point source, plus FBM domain-warp noise for organic
// irregularity, plus a "chaotic push" ring term for the shockwave look.
//
// Single puff only — user explicitly wanted one soft puff, not several
// combined shapes. A "bigger cloud" is composed by calling this function
// multiple times with different pos/scale/phase from the call site.
void VFX_ComposeEnergySmoke(Vector3 pos, float scale, float progress, float time, Vector2 sourceUV)
{
    (void)time; // shader reads the auto-bound u_time uniform (SkillManager_BeginShader) instead
    if (scale <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);

    bool localBatch = !s_smokeBatchActive;
    if (localBatch)
    {
        VFX_BeginEnergySmokeBatch();
    }

    // D calibrated against t0=0.005 (energy_smoke.fs) so half-width goes
    // from ~0.05 (tight dot) at birth to ~0.7 (fills most of the quad) at
    // death — was 0.35 paired with the old t0=0.05, which started already
    // half-spread. try 0.10 (slow, stays tighter) .. 0.30 (fast, wide).
    SmokeShader_SetUniforms((Color){255, 255, 255, 200}, progress, 0.18f, 3.0f, 0.5f, sourceUV);

    DrawCoreBillboardQuad(pos, scale, camera, WHITE);

    if (localBatch)
    {
        VFX_EndEnergySmokeBatch();
    }
}

// Ground-hugging gas patch (dry-ice-on-the-floor look) — same energy_smoke.fs
// puff/ring density field as VFX_ComposeEnergySmoke, but drawn on a
// horizontal, terrain-conforming DrawCoreGroundPatch instead of a
// camera-facing billboard. heightFn/userData let it follow sloped/heightmap
// ground (NULL heightFn = flat, matches this project's "Y=0 ground level"
// convention for arena maps). Reuses the puff shader's static instance —
// same shape logic, only the geometry differs, so no separate shader needed
// here (unlike the column, which needed genuinely different physics).
void VFX_ComposeGroundSmoke(Vector3 center, float halfSize, float progress,
                            GroundHeightSampleFn heightFn, void *userData)
{
    if (halfSize <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);
    SmokeShader_Init();

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    SmokeShader_SetUniforms((Color){225, 235, 235, 190}, progress, 0.20f, 3.5f, 0.4f, (Vector2){0.0f, 0.0f});

    // subdiv=6 -> 7x7 vertex grid, plenty to follow a gentle slope under a
    // small (~1-2m) patch while staying cheap (72 tris). yLift=0.03: without
    // it the patch sits exactly coplanar with real ground geometry and
    // z-fights against it — reads as a jagged hard-edged clip through the
    // smoke, not a shader/shape bug (see core/decal_system.c's own 0.02f
    // yOffset for the same reason on ground decals).
    DrawCoreGroundPatch(center, halfSize, 6, 0.03f, heightFn, userData, WHITE);
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
}

// Gas patch on an ARBITRARY oriented surface — same puff density field as
// VFX_ComposeGroundSmoke, but takes `normal` explicitly instead of assuming
// horizontal Y-up ground, so it can sit on a sloped rock face, a wall, a
// ceiling. Single flat quad (DrawCoreOrientedQuad), no per-vertex heightFn/
// terrain-conforming — that's specifically a ground/Y-up concept (matches
// MapProp_SampleGroundHeight's absolute-Y contract, which doesn't
// generalize to an arbitrary normal without real surface-following data to
// sample). `normal` also picks the small anti-z-fighting lift direction
// (0.03m along normal instead of a hardcoded +Y).
void VFX_ComposeSmokeOnPlane(Vector3 center, Vector3 normal, float halfSize, float progress, Color color)
{
    if (halfSize <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);
    SmokeShader_Init();

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    SmokeShader_SetUniforms(color, progress, 0.20f, 3.5f, 0.4f, (Vector2){0.0f, 0.0f});

    Vector3 n = Vector3Normalize(normal);
    Vector3 lifted = Vector3Add(center, Vector3Scale(n, 0.03f)); // avoid z-fighting with the surface it sits on
    DrawCoreOrientedQuad(lifted, n, halfSize, WHITE);
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
}

// ── Smoke column (smoke_column.fs) ──────────────────────────────────────────────

static Shader s_columnSh = {0};
static int s_colUColor = -1;
static int s_colUProgress = -1;
static int s_colUDiffusion = -1;
static int s_colUNoiseScale = -1;
static int s_colUDriftSpeed = -1;
static int s_colURiseSpeed = -1;
static int s_colUSeed = -1;
static bool s_columnInit = false;

// Batch and Uniform State Caching to prevent Raylib batching hazards / redundant GL state changes
static bool s_batchActive = false;
static float s_lastProgress = -1.0f;
static float s_lastDiffusion = -1.0f;
static float s_lastNoiseScale = -1.0f;
static float s_lastDriftSpeed = -1.0f;
static float s_lastRiseSpeed = -1.0f;
static Vector4 s_lastColorV = {0.0f, 0.0f, 0.0f, 0.0f};

static void SmokeColumnShader_Init(void)
{
    if (s_columnInit)
        return;
    s_columnSh = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                            "core/shaders/smoke_column.fs");
    s_colUColor = GetShaderLocation(s_columnSh, "u_color");
    s_colUProgress = GetShaderLocation(s_columnSh, "u_progress");
    s_colUDiffusion = GetShaderLocation(s_columnSh, "u_diffusion");
    s_colUNoiseScale = GetShaderLocation(s_columnSh, "u_noiseScale");
    s_colUDriftSpeed = GetShaderLocation(s_columnSh, "u_driftSpeed");
    s_colURiseSpeed = GetShaderLocation(s_columnSh, "u_riseSpeed");
    s_colUSeed = GetShaderLocation(s_columnSh, "u_seed");
    s_columnInit = true;
}

void VFX_BeginSmokeColumnBatch(void)
{
    if (s_batchActive)
        return;
    SmokeColumnShader_Init();
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_columnSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    s_batchActive = true;

    // Reset caches on begin
    s_lastProgress = -1.0f;
    s_lastDiffusion = -1.0f;
    s_lastNoiseScale = -1.0f;
    s_lastDriftSpeed = -1.0f;
    s_lastRiseSpeed = -1.0f;
    s_lastColorV = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
}

void VFX_EndSmokeColumnBatch(void)
{
    if (!s_batchActive)
        return;
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
    s_batchActive = false;
}

static void DrawCoreCrossQuadsWithSeed(Vector3 base, float halfWidth, float height, int planeCount, float seed)
{
    if (planeCount < 1) return;
    float normSeed = seed / 100.0f;

    for (int i = 0; i < planeCount; i++)
    {
        float theta = (PI * i) / planeCount; // planes are full rectangles, so only need to span 180°
        Vector3 right = { cosf(theta), 0.0f, sinf(theta) };
        Vector3 n     = { -sinf(theta), 0.0f, cosf(theta) }; // arbitrary but consistent facing for lighting

        Vector3 p00 = Vector3Add(base, Vector3Scale(right, -halfWidth));
        Vector3 p10 = Vector3Add(base, Vector3Scale(right,  halfWidth));
        Vector3 p11 = Vector3Add(p10, (Vector3){0.0f, height, 0.0f});
        Vector3 p01 = Vector3Add(p00, (Vector3){0.0f, height, 0.0f});

        rlBegin(RL_QUADS);
        rlColor4ub(WHITE.r, WHITE.g, WHITE.b, WHITE.a);
        rlNormal3f(n.x, normSeed, n.z); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p00.x, p00.y, p00.z);
        rlNormal3f(n.x, normSeed, n.z); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p10.x, p10.y, p10.z);
        rlNormal3f(n.x, normSeed, n.z); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p11.x, p11.y, p11.z);
        rlNormal3f(n.x, normSeed, n.z); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p01.x, p01.y, p01.z);
        rlEnd();
    }
}

// Long rising column (cigarette-smoke style) — 2-3 fixed vertical "cross
// billboard" rectangles through `base` (DrawCoreCrossQuads), shaded by
// smoke_column.fs: a blob that starts at the bottom edge, climbs, widens,
// and fades — no ring/hollow-center logic (that's energy_smoke.fs's job,
// for the unrelated static-point shockwave-puff look).
//
// `progress` is expected to be a REPEATING ramp (e.g. fract(elapsed *
// loopSpeed) from the caller), not a one-shot 0->1 — a continuously-existing
// column has no natural "death" moment, so the birth->climb->dissolve curve
// is looped instead. u_time keeps advancing independently every frame
// (auto-bound, not reset by the progress loop), so the FBM warp underneath
// never repeats identically — successive loops look like genuinely
// different smoke, not an obvious replay.
void VFX_ComposeSmokeColumnFX(Vector3 base, float halfWidth, float height, float progress, int planeCount)
{
    if (halfWidth <= 0.0f || height <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);
    if (planeCount < 1)
        planeCount = 2;

    bool localBatch = !s_batchActive;
    if (localBatch)
    {
        VFX_BeginSmokeColumnBatch();
    }

    // Set uniforms (only flush and upload if values changed to avoid redundant draw calls/flushes)
    if (progress != s_lastProgress)
    {
        rlDrawRenderBatchActive();
        if (s_colUProgress >= 0)
            SetShaderValue(s_columnSh, s_colUProgress, &progress, SHADER_UNIFORM_FLOAT);
        s_lastProgress = progress;
    }

    float diffusion = 0.10f;
    if (diffusion != s_lastDiffusion)
    {
        if (s_colUDiffusion >= 0)
            SetShaderValue(s_columnSh, s_colUDiffusion, &diffusion, SHADER_UNIFORM_FLOAT);
        s_lastDiffusion = diffusion;
    }

    float noiseScale = 2.5f;
    if (noiseScale != s_lastNoiseScale)
    {
        if (s_colUNoiseScale >= 0)
            SetShaderValue(s_columnSh, s_colUNoiseScale, &noiseScale, SHADER_UNIFORM_FLOAT);
        s_lastNoiseScale = noiseScale;
    }

    float driftSpeed = 5.0f;
    if (driftSpeed != s_lastDriftSpeed)
    {
        if (s_colUDriftSpeed >= 0)
            SetShaderValue(s_columnSh, s_colUDriftSpeed, &driftSpeed, SHADER_UNIFORM_FLOAT);
        s_lastDriftSpeed = driftSpeed;
    }

    float riseSpeed = 15.0f;
    if (riseSpeed != s_lastRiseSpeed)
    {
        if (s_colURiseSpeed >= 0)
            SetShaderValue(s_columnSh, s_colURiseSpeed, &riseSpeed, SHADER_UNIFORM_FLOAT);
        s_lastRiseSpeed = riseSpeed;
    }

    Vector4 colorV = ColorNormalize((Color){200, 200, 200, 150});
    if (colorV.x != s_lastColorV.x || colorV.y != s_lastColorV.y || colorV.z != s_lastColorV.z || colorV.w != s_lastColorV.w)
    {
        if (s_colUColor >= 0)
            SetShaderValue(s_columnSh, s_colUColor, &colorV, SHADER_UNIFORM_VEC4);
        s_lastColorV = colorV;
    }

    float seed = fmodf(fabsf(sinf(base.x * 12.9898f + base.y * 57.293f + base.z * 78.233f) * 43758.5453f), 1000.0f);

    if (planeCount == 1)
    {
        // Draw a single Y-axis cylindrical billboard (rotates to face the camera horizontally)
        // This ensures the smoke column is always facing the camera and looks volumetric,
        // while only rendering 1 quad (saving 66% of vertex/pixel shader cost).
        Vector3 camPos = camera.position;
        Vector2 toCam = { camPos.x - base.x, camPos.z - base.z };
        float len = sqrtf(toCam.x * toCam.x + toCam.y * toCam.y);
        Vector3 right;
        if (len > 0.001f)
        {
            toCam.x /= len;
            toCam.y /= len;
            right = (Vector3){ -toCam.y, 0.0f, toCam.x };
        }
        else
        {
            right = (Vector3){ 1.0f, 0.0f, 0.0f };
        }

        Vector3 p00 = Vector3Add(base, Vector3Scale(right, -halfWidth));
        Vector3 p10 = Vector3Add(base, Vector3Scale(right,  halfWidth));
        Vector3 p11 = Vector3Add(p10, (Vector3){0.0f, height, 0.0f});
        Vector3 p01 = Vector3Add(p00, (Vector3){0.0f, height, 0.0f});

        rlBegin(RL_QUADS);
        rlColor4ub(WHITE.r, WHITE.g, WHITE.b, WHITE.a);
        rlNormal3f(toCam.x, seed / 100.0f, toCam.y); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p00.x, p00.y, p00.z);
        rlNormal3f(toCam.x, seed / 100.0f, toCam.y); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p10.x, p10.y, p10.z);
        rlNormal3f(toCam.x, seed / 100.0f, toCam.y); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p11.x, p11.y, p11.z);
        rlNormal3f(toCam.x, seed / 100.0f, toCam.y); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p01.x, p01.y, p01.z);
        rlEnd();
    }
    else
    {
        DrawCoreCrossQuadsWithSeed(base, halfWidth, height, planeCount, seed);
    }

    if (localBatch)
    {
        VFX_EndSmokeColumnBatch();
    }
}

// ── Magical Sparkling Filaments (magic_filaments.fs) ──────────────────────────

static Shader s_filamentSh = {0};
static int s_filUColor = -1;
static int s_filUProgress = -1;
static int s_filUDiffusion = -1;
static int s_filUNoiseScale = -1;
static int s_filUDriftSpeed = -1;
static int s_filUSourcePos = -1;
static bool s_filamentInit = false;

static bool s_filamentBatchActive = false;
static Color s_lastFilamentTint = {0, 0, 0, 0};
static float s_lastFilamentProgress = -1.0f;
static float s_lastFilamentDiffusion = -1.0f;
static float s_lastFilamentNoiseScale = -1.0f;
static float s_lastFilamentDriftSpeed = -1.0f;
static Vector2 s_lastFilamentSourceUV = {-1.0f, -1.0f};

static void FilamentShader_Init(void)
{
    if (s_filamentInit)
        return;
    s_filamentSh = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                              "core/shaders/magic_filaments.fs");
    s_filUColor = GetShaderLocation(s_filamentSh, "u_color");
    s_filUProgress = GetShaderLocation(s_filamentSh, "u_progress");
    s_filUDiffusion = GetShaderLocation(s_filamentSh, "u_diffusion");
    s_filUNoiseScale = GetShaderLocation(s_filamentSh, "u_noiseScale");
    s_filUDriftSpeed = GetShaderLocation(s_filamentSh, "u_driftSpeed");
    s_filUSourcePos = GetShaderLocation(s_filamentSh, "u_sourcePos");
    s_filamentInit = true;
}

void VFX_BeginMagicFilamentsBatch(void)
{
    if (s_filamentBatchActive)
        return;
    FilamentShader_Init();
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_filamentSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    s_filamentBatchActive = true;

    // Reset caches on begin
    s_lastFilamentProgress = -1.0f;
    s_lastFilamentDiffusion = -1.0f;
    s_lastFilamentNoiseScale = -1.0f;
    s_lastFilamentDriftSpeed = -1.0f;
    s_lastFilamentSourceUV = (Vector2){-1.0f, -1.0f};
    s_lastFilamentTint = (Color){0, 0, 0, 0};
}

void VFX_EndMagicFilamentsBatch(void)
{
    if (!s_filamentBatchActive)
        return;
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
    s_filamentBatchActive = false;
}

static void FilamentShader_SetUniforms(Color tint, float progress, float diffusion,
                                       float noiseScale, float driftSpeed, Vector2 sourceUV)
{
    bool changed = (progress != s_lastFilamentProgress) || 
                   (diffusion != s_lastFilamentDiffusion) ||
                   (noiseScale != s_lastFilamentNoiseScale) ||
                   (driftSpeed != s_lastFilamentDriftSpeed) ||
                   (sourceUV.x != s_lastFilamentSourceUV.x || sourceUV.y != s_lastFilamentSourceUV.y) ||
                   (tint.r != s_lastFilamentTint.r || tint.g != s_lastFilamentTint.g || tint.b != s_lastFilamentTint.b || tint.a != s_lastFilamentTint.a);

    if (changed)
    {
        rlDrawRenderBatchActive(); // Flush before uploading new uniforms

        if (progress != s_lastFilamentProgress)
        {
            if (s_filUProgress >= 0)
                SetShaderValue(s_filamentSh, s_filUProgress, &progress, SHADER_UNIFORM_FLOAT);
            s_lastFilamentProgress = progress;
        }
        if (diffusion != s_lastFilamentDiffusion)
        {
            if (s_filUDiffusion >= 0)
                SetShaderValue(s_filamentSh, s_filUDiffusion, &diffusion, SHADER_UNIFORM_FLOAT);
            s_lastFilamentDiffusion = diffusion;
        }
        if (noiseScale != s_lastFilamentNoiseScale)
        {
            if (s_filUNoiseScale >= 0)
                SetShaderValue(s_filamentSh, s_filUNoiseScale, &noiseScale, SHADER_UNIFORM_FLOAT);
            s_lastFilamentNoiseScale = noiseScale;
        }
        if (driftSpeed != s_lastFilamentDriftSpeed)
        {
            if (s_filUDriftSpeed >= 0)
                SetShaderValue(s_filamentSh, s_filUDriftSpeed, &driftSpeed, SHADER_UNIFORM_FLOAT);
            s_lastFilamentDriftSpeed = driftSpeed;
        }
        if (sourceUV.x != s_lastFilamentSourceUV.x || sourceUV.y != s_lastFilamentSourceUV.y)
        {
            if (s_filUSourcePos >= 0)
                SetShaderValue(s_filamentSh, s_filUSourcePos, &sourceUV, SHADER_UNIFORM_VEC2);
            s_lastFilamentSourceUV = sourceUV;
        }
        if (tint.r != s_lastFilamentTint.r || tint.g != s_lastFilamentTint.g || tint.b != s_lastFilamentTint.b || tint.a != s_lastFilamentTint.a)
        {
            Vector4 colorV = ColorNormalize(tint);
            if (s_filUColor >= 0)
                SetShaderValue(s_filamentSh, s_filUColor, &colorV, SHADER_UNIFORM_VEC4);
            s_lastFilamentTint = tint;
        }
    }
}

void VFX_ComposeMagicFilaments(Vector3 pos, float scale, float progress, Color color, float thickness, float frequency, float speed, Vector2 sourceUV)
{
    if (scale <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);

    bool localBatch = !s_filamentBatchActive;
    if (localBatch)
    {
        VFX_BeginMagicFilamentsBatch();
    }

    // thickness maps to u_diffusion (which controls exponents/sharpness of the ridged lines)
    // frequency maps to u_noiseScale
    // speed maps to u_driftSpeed
    FilamentShader_SetUniforms(color, progress, thickness, frequency, speed, sourceUV);

    DrawCoreBillboardQuad(pos, scale, camera, WHITE);

    if (localBatch)
    {
        VFX_EndMagicFilamentsBatch();
    }
}

void VFX_ComposeMagicFilamentsOnPlane(Vector3 center, Vector3 normal, float scale, float progress, Color color, float thickness, float frequency, float speed, Vector2 sourceUV)
{
    if (scale <= 0.0f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);

    bool localBatch = !s_filamentBatchActive;
    if (localBatch)
    {
        VFX_BeginMagicFilamentsBatch();
    }

    FilamentShader_SetUniforms(color, progress, thickness, frequency, speed, sourceUV);

    Vector3 n = Vector3Normalize(normal);
    Vector3 lifted = Vector3Add(center, Vector3Scale(n, 0.03f)); // avoid z-fighting with the surface it sits on
    DrawCoreOrientedQuad(lifted, n, scale, WHITE);

    if (localBatch)
    {
        VFX_EndMagicFilamentsBatch();
    }
}

