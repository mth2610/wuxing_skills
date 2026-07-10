// Energy Smoke — ONE puff that blooms out slowly and hazily then dissolves.
// Shader-driven (core/shaders/energy_smoke.fs) — no texture atlas, no
// particle system, no external asset dependency (see the flipbook/video
// asset-sourcing detour this replaced).
//
// Drawn on a camera-facing billboard quad (DrawCoreBillboardQuad), not a
// sphere mesh — a sphere's silhouette is always a hard geometric circle
// in screen space regardless of surface shading; a flat quad has no such
// constraint, so the shape falls entirely out of the shader's own alpha.
// Cheaper too: 1 quad (2 triangles) vs a 20x20 sphere (800 tris).
//
// energy_smoke.fs is the closed-form analytic solution to the 2D diffusion
// equation for a point source (a time-widening, amplitude-falling Gaussian)
// plus FBM domain-warp noise on top for organic irregularity — not a
// numeric grid solver (would need per-puff render-texture ping-pong, real
// but much heavier than this needs) and not a raymarch (tried, but a
// closed-form solution is both cheaper and a better physical match for
// "diffusing smoke" specifically).
//
// Single puff only — user explicitly wanted one soft puff, not several
// combined shapes. A "bigger cloud" is composed by calling this function
// multiple times with different pos/scale/phase from the call site.
//
// sourceUV picks where inside the quad's [-1,1] local space the puff
// originates: {0,0} = center (radial puff/shockwave ring), {0,-1} = base
// (bottom edge) — the latter is what a rising column (e.g. cigarette
// smoke) needs, since the puff must grow upward from a fixed foot point
// rather than expand symmetrically in every direction.
void VFX_ComposeEnergySmoke(Vector3 pos, float scale, float progress, float time, Vector2 sourceUV)
{
    if (scale <= 0.0f) return;
    progress = Clamp(progress, 0.0f, 1.0f);

    static Shader s_smokeSh     = {0};
    static int    s_uColor      = -1;
    static int    s_uProgress   = -1;
    static int    s_uDiffusion  = -1;
    static int    s_uNoiseScale = -1;
    static int    s_uDriftSpeed = -1;
    static int    s_uSourcePos  = -1;
    static bool   s_smokeInit   = false;
    if (!s_smokeInit)
    {
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

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_smokeSh);
    BeginBlendMode(BLEND_ALPHA); // translucent haze, not a glowing additive blob
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // Tint at the caller by scaling these RGB values if a non-white smoke
    // is ever needed; the shader itself stays color-agnostic (u_color passthrough).
    Vector4 colorV = ColorNormalize((Color){255, 255, 255, 200});
    if (s_uColor >= 0) SetShaderValue(s_smokeSh, s_uColor, &colorV, SHADER_UNIFORM_VEC4);

    float r          = scale; // quad's fixed physical footprint — the diffusion
                              // equation itself controls the visible spread inside it
    // D calibrated against t0=0.005 (energy_smoke.fs) so half-width goes
    // from ~0.05 (tight dot) at birth to ~0.7 (fills most of the quad) at
    // death — was 0.35 paired with the old t0=0.05, which started already
    // half-spread. try 0.10 (slow, stays tighter) .. 0.30 (fast, wide).
    float diffusion  = 0.18f;
    float noiseScale = 3.0f;  // turbulence warp domain frequency
    float driftSpeed = 0.5f;  // turbulence warp animation speed

    if (s_uProgress   >= 0) SetShaderValue(s_smokeSh, s_uProgress,   &progress,    SHADER_UNIFORM_FLOAT);
    if (s_uDiffusion  >= 0) SetShaderValue(s_smokeSh, s_uDiffusion,  &diffusion,   SHADER_UNIFORM_FLOAT);
    if (s_uNoiseScale >= 0) SetShaderValue(s_smokeSh, s_uNoiseScale, &noiseScale,  SHADER_UNIFORM_FLOAT);
    if (s_uDriftSpeed >= 0) SetShaderValue(s_smokeSh, s_uDriftSpeed, &driftSpeed,  SHADER_UNIFORM_FLOAT);
    if (s_uSourcePos  >= 0) SetShaderValue(s_smokeSh, s_uSourcePos,  &sourceUV,    SHADER_UNIFORM_VEC2);

    DrawCoreBillboardQuad(pos, r, camera, WHITE);
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
}
