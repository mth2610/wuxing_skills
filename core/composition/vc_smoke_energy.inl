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

static Shader s_smokeSh     = {0};
static int    s_uColor      = -1;
static int    s_uProgress   = -1;
static int    s_uDiffusion  = -1;
static int    s_uNoiseScale = -1;
static int    s_uDriftSpeed = -1;
static int    s_uSourcePos  = -1;
static bool   s_smokeInit   = false;

static void SmokeShader_Init(void)
{
    if (s_smokeInit) return;
    s_smokeSh      = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                                 "core/shaders/energy_smoke.fs");
    s_uColor       = GetShaderLocation(s_smokeSh, "u_color");
    s_uProgress    = GetShaderLocation(s_smokeSh, "u_progress");
    s_uDiffusion   = GetShaderLocation(s_smokeSh, "u_diffusion");
    s_uNoiseScale  = GetShaderLocation(s_smokeSh, "u_noiseScale");
    s_uDriftSpeed  = GetShaderLocation(s_smokeSh, "u_driftSpeed");
    s_uSourcePos   = GetShaderLocation(s_smokeSh, "u_sourcePos");
    s_smokeInit    = true;
}

static void SmokeShader_SetUniforms(Color tint, float progress, float diffusion,
                                    float noiseScale, float driftSpeed, Vector2 sourceUV)
{
    Vector4 colorV = ColorNormalize(tint);
    if (s_uColor      >= 0) SetShaderValue(s_smokeSh, s_uColor,      &colorV,     SHADER_UNIFORM_VEC4);
    if (s_uProgress   >= 0) SetShaderValue(s_smokeSh, s_uProgress,   &progress,   SHADER_UNIFORM_FLOAT);
    if (s_uDiffusion  >= 0) SetShaderValue(s_smokeSh, s_uDiffusion,  &diffusion,  SHADER_UNIFORM_FLOAT);
    if (s_uNoiseScale >= 0) SetShaderValue(s_smokeSh, s_uNoiseScale, &noiseScale, SHADER_UNIFORM_FLOAT);
    if (s_uDriftSpeed >= 0) SetShaderValue(s_smokeSh, s_uDriftSpeed, &driftSpeed, SHADER_UNIFORM_FLOAT);
    if (s_uSourcePos  >= 0) SetShaderValue(s_smokeSh, s_uSourcePos,  &sourceUV,   SHADER_UNIFORM_VEC2);
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
    if (scale <= 0.0f) return;
    progress = Clamp(progress, 0.0f, 1.0f);
    SmokeShader_Init();

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA); // translucent haze, not a glowing additive blob
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // D calibrated against t0=0.005 (energy_smoke.fs) so half-width goes
    // from ~0.05 (tight dot) at birth to ~0.7 (fills most of the quad) at
    // death — was 0.35 paired with the old t0=0.05, which started already
    // half-spread. try 0.10 (slow, stays tighter) .. 0.30 (fast, wide).
    SmokeShader_SetUniforms((Color){255, 255, 255, 200}, progress, 0.18f, 3.0f, 0.5f, sourceUV);

    DrawCoreBillboardQuad(pos, scale, camera, WHITE);
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
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
    if (halfSize <= 0.0f) return;
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
void VFX_ComposeSmokeOnPlane(Vector3 center, Vector3 normal, float halfSize, float progress)
{
    if (halfSize <= 0.0f) return;
    progress = Clamp(progress, 0.0f, 1.0f);
    SmokeShader_Init();

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    SmokeShader_SetUniforms((Color){225, 235, 235, 190}, progress, 0.20f, 3.5f, 0.4f, (Vector2){0.0f, 0.0f});

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

static Shader s_columnSh      = {0};
static int    s_colUColor     = -1;
static int    s_colUProgress  = -1;
static int    s_colUDiffusion = -1;
static int    s_colUNoiseScale= -1;
static int    s_colUDriftSpeed= -1;
static int    s_colURiseSpeed = -1;
static bool   s_columnInit    = false;

static void SmokeColumnShader_Init(void)
{
    if (s_columnInit) return;
    s_columnSh      = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                                 "core/shaders/smoke_column.fs");
    s_colUColor      = GetShaderLocation(s_columnSh, "u_color");
    s_colUProgress   = GetShaderLocation(s_columnSh, "u_progress");
    s_colUDiffusion  = GetShaderLocation(s_columnSh, "u_diffusion");
    s_colUNoiseScale = GetShaderLocation(s_columnSh, "u_noiseScale");
    s_colUDriftSpeed = GetShaderLocation(s_columnSh, "u_driftSpeed");
    s_colURiseSpeed  = GetShaderLocation(s_columnSh, "u_riseSpeed");
    s_columnInit     = true;
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
    if (halfWidth <= 0.0f || height <= 0.0f) return;
    progress = Clamp(progress, 0.0f, 1.0f);
    if (planeCount < 1) planeCount = 2;
    SmokeColumnShader_Init();

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_columnSh);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    Vector4 colorV = ColorNormalize((Color){200, 200, 200, 150});
    float diffusion  = 0.10f; // lateral spread — smaller than the puff, reads as a narrow wisp
    float noiseScale = 4.5f;  // wider than the puff so the warp reads at the tall/narrow aspect
    float driftSpeed = 0.5f;
    float riseSpeed  = 1.8f;  // climbs from uv.y=-1 to +0.8 over one loop — most of the quad's height

    if (s_colUColor      >= 0) SetShaderValue(s_columnSh, s_colUColor,      &colorV,     SHADER_UNIFORM_VEC4);
    if (s_colUProgress   >= 0) SetShaderValue(s_columnSh, s_colUProgress,   &progress,   SHADER_UNIFORM_FLOAT);
    if (s_colUDiffusion  >= 0) SetShaderValue(s_columnSh, s_colUDiffusion,  &diffusion,  SHADER_UNIFORM_FLOAT);
    if (s_colUNoiseScale >= 0) SetShaderValue(s_columnSh, s_colUNoiseScale, &noiseScale, SHADER_UNIFORM_FLOAT);
    if (s_colUDriftSpeed >= 0) SetShaderValue(s_columnSh, s_colUDriftSpeed, &driftSpeed, SHADER_UNIFORM_FLOAT);
    if (s_colURiseSpeed  >= 0) SetShaderValue(s_columnSh, s_colURiseSpeed,  &riseSpeed,  SHADER_UNIFORM_FLOAT);

    DrawCoreCrossQuads(base, halfWidth, height, planeCount, WHITE);
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
}
