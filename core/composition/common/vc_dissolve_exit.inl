// ── E5.4 — VFX_ComposeDissolveExit ───────────────────────────────────────────
//
// The shared erosion-out. ER effects never POP off: they erode from the inside
// out with a bright leading edge, and whatever is left drifts away as embers.
// Any effect can call this on its death instead of inventing its own fade.
//
// `core/shaders/dissolve.fs` already existed and had NO consumer anywhere in the
// project — the spec's task here is to expose it as a composition, which is why
// this component uses a shader at all where the rest of Đợt E uses particles.
// It is a shader that erodes an ALPHA MASK, not another one-surface-plus-FBM
// aura (§0.1b): the noise decides *when* each texel leaves, never what the shape
// is, and the shape itself comes from the sprite cluster.
//
// TWO POPULATIONS, per the blend law (§F1b):
//   - the BODY erodes under BLEND_ALPHA — it still occludes while it exists;
//   - the EMBERS it sheds are additive + unlit with an emissiveBoost, because
//     they emit. One draw call cannot be both, which is the whole point of F1b.

#define DISSOLVE_QUADS 5

static Shader    s_dissolveSh   = {0};
static int       s_dsLocAmount  = -1;
static int       s_dsLocEdgeW   = -1;
static int       s_dsLocEdgeCol = -1;
static int       s_dsLocNoise   = -1;
static int       s_dsLocGrain   = -1;
static int       s_dsLocOffset  = -1;
static Texture2D s_dsNoiseTex   = {0};
static Texture2D s_dsBodyTex    = {0};
static bool      s_dissolveInit = false;

static float s_dissolveEdge  = 0.09f;  // width of the glowing rim band
static float s_dissolveEmber = 1.0f;   // x on ember count (0 = body only)
static float s_dissolveGrain = 0.42f;  // <1 = bigger clumps, 1 = finest grain

// A soft round mask for the body. Generated rather than loaded: the component
// has to work with no asset (spec), and the stock particle texture is exactly
// this shape anyway — building it keeps the dependency at zero.
static void Dissolve_BuildBodyTexture(void)
{
    const int S = 64;
    Image img = GenImageColor(S, S, BLANK);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        {
            float u = ((float)x + 0.5f) / S * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / S * 2.0f - 1.0f;
            float d = sqrtf(u * u + v * v);
            float a = 1.0f - (d > 1.0f ? 1.0f : d);
            a = a * a;                       // soft shoulder, no hard rim
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});
        }
    s_dsBodyTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_dsBodyTex.id != 0) SetTextureFilter(s_dsBodyTex, TEXTURE_FILTER_BILINEAR);
}

static void Dissolve_InitShared(void)
{
    if (s_dissolveInit) return;

    // NULL vertex shader = raylib's default, which is what dissolve.fs is
    // written against (it reads fragTexCoord/fragColor/colDiffuse, all of them
    // the default pipeline's names).
    s_dissolveSh   = ResourceManager_LoadShader(NULL, "core/shaders/dissolve.fs");
    s_dsLocAmount  = GetShaderLocation(s_dissolveSh, "dissolveAmount");
    s_dsLocEdgeW   = GetShaderLocation(s_dissolveSh, "edgeWidth");
    s_dsLocEdgeCol = GetShaderLocation(s_dissolveSh, "edgeColor");
    s_dsLocNoise   = GetShaderLocation(s_dissolveSh, "noiseTex");
    s_dsLocGrain   = GetShaderLocation(s_dissolveSh, "noiseScale");
    s_dsLocOffset  = GetShaderLocation(s_dissolveSh, "noiseOffset");

    s_dsNoiseTex = ResourceManager_LoadTexture("assets/textures/noise.png");
    if (s_dsNoiseTex.id == 0)
        TraceLog(LOG_WARNING, "DissolveExit: noise.png missing — erosion will be uniform");
    else
    {
        SetTextureWrap(s_dsNoiseTex, TEXTURE_WRAP_REPEAT);
        // BILINEAR, and it is not optional here. raylib defaults to POINT, and
        // this shader MAGNIFIES the noise (noiseScale < 1) across a metre-wide
        // sprite — at POINT every one of the 256x256 texels becomes a visible
        // square, so the erosion edge comes out as staircase blocks instead of
        // a torn edge. Filtering happens before the threshold comparison, so it
        // smooths the CUT itself, not just the picture.
        SetTextureFilter(s_dsNoiseTex, TEXTURE_FILTER_BILINEAR);
    }

    Dissolve_BuildBodyTexture();

    Tuning_RegisterFloat("dissolve_edge",  &s_dissolveEdge,  0.09f);
    Tuning_RegisterFloat("dissolve_ember", &s_dissolveEmber, 1.0f);
    Tuning_RegisterFloat("dissolve_grain", &s_dissolveGrain, 0.42f);

    s_dissolveInit = true;
}

// Continuous while dying: call every frame with `t01` running 0 → 1.
// `scale` = the dying effect's radius in metres. Attach to ANY effect's death.
void VFX_ComposeDissolveExit(Vector3 pos, VC_MaterialId mat, float scale, float t01)
{
    Dissolve_InitShared();
    if (scale <= 0.0f) scale = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float time = (float)GetTime();

    // ── Body: erodes away under the dissolve shader ──────────────────────────
    if (s_dissolveSh.id != 0)
    {
        rlDrawRenderBatchActive();
        BeginShaderMode(s_dissolveSh);
        // Uniforms are set INSIDE the shader mode: under rlvk SetShaderValue
        // writes to whichever shader is ACTIVE, so setting them earlier lands
        // them on the previous shader, silently (E1's landmine).
        float edgeW = s_dissolveEdge;
        Vector4 edge = { m->glow.r / 255.0f, m->glow.g / 255.0f,
                         m->glow.b / 255.0f, 1.0f };
        // t01 is remapped onto the noise's ACTUAL value range instead of being
        // fed to the threshold raw. Measured on assets/textures/noise.png:
        // mean 0.498, p10 0.310, p90 0.686 — a narrow distribution, so a raw
        // threshold does nothing at all below ~0.31, erases almost everything
        // between 0.31 and 0.69, and has nothing left to do above it. The
        // erosion therefore appeared to happen in a 40% window in the middle.
        // Mapping onto [0.18, 0.86] spends the whole of t01 on visible change.
        float amount = Math_Mix(0.18f, 0.86f, t01);
        SetShaderValue(s_dissolveSh, s_dsLocAmount, &amount, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_dissolveSh, s_dsLocEdgeW, &edgeW, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_dissolveSh, s_dsLocEdgeCol, &edge, SHADER_UNIFORM_VEC4);
        if (s_dsNoiseTex.id != 0)
            SetShaderValueTexture(s_dissolveSh, s_dsLocNoise, s_dsNoiseTex);

        // ALPHA, not additive: a body that still occludes is the only reason
        // the erosion reads as matter being eaten away rather than a light
        // going out.
        VFXRenderScope renderScope = VFXRender_BeginDraw(
            VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);

        SetShaderValue(s_dissolveSh, s_dsLocGrain, &s_dissolveGrain, SHADER_UNIFORM_FLOAT);

        rlSetTexture(s_dsBodyTex.id);
        for (int i = 0; i < DISSOLVE_QUADS; i++)
        {
            // Each sprite reads a different patch of the noise, so the cluster
            // does not erode as one stencilled shape. The batch has to be
            // flushed around the change or the new offset would retro-apply to
            // the quads already queued (ENGINE_LANDMINES §1).
            Vector2 off = { (float)i * 0.37f, (float)i * 0.61f };
            rlDrawRenderBatchActive();
            SetShaderValue(s_dissolveSh, s_dsLocOffset, &off, SHADER_UNIFORM_VEC2);
            // Re-bind AFTER the flush: rlDrawRenderBatchActive resets the
            // current texture to rlgl's default 1x1 white. Binding once before
            // the loop meant every quad after the first sampled solid white —
            // alpha 1 everywhere — and the soft round body rendered as a hard
            // bright SQUARE.
            rlSetTexture(s_dsBodyTex.id);

            // Cluster rather than one quad: a single sprite has no internal
            // parallax and reads as a decal being wiped (§0.1b, cause 4). Each
            // quad sits on its own offset and drifts a little as it goes.
            float ph = (float)i * 1.7f;
            Vector3 p = VC_MotionJitter(pos, scale * 0.30f, 0.55f, time * 0.35f + ph, ph);
            p.y += scale * 0.35f * t01;          // the whole cluster lifts as it dies
            float r = scale * (0.55f + 0.22f * VC_Flicker01((float)i, 3.1f));
            // Swelling slightly while eroding — matter expanding as it loses
            // cohesion. Shrinking instead reads as a zoom-out.
            r *= 1.0f + 0.35f * t01;
            Color c = VC_WithAlpha(m->body, (unsigned char)(215 * (1.0f - 0.25f * t01)));
            DrawCoreBillboardQuad(p, r, camera, c);
        }
        rlSetTexture(0);

        VFXRender_EndDraw(&renderScope);
        EndShaderMode();
    }

    // ── Embers: what the erosion sheds ───────────────────────────────────────
    // Rate peaks in the MIDDLE of the dissolve, where the erosion front is
    // sweeping the widest part of the body — not at the start (nothing has
    // eroded yet) and not at the end (there is nothing left to shed).
    if (s_dissolveEmber > 0.01f)
    {
        static float s_emberAccum = 0.0f;
        float front = 4.0f * t01 * (1.0f - t01);        // 0 → 1 → 0
        s_emberAccum += GetFrameTime() * (46.0f * front * s_dissolveEmber);
        int n = (int)s_emberAccum;
        if (n > 8) n = 8;
        s_emberAccum -= (float)n;

        for (int e = 0; e < n; e++)
        {
            // Born on the shell of what is currently eroding, so the embers
            // trace the front rather than the original silhouette.
            Vector3 dir = VC_DirCone((Vector3){0.0f, 1.0f, 0.0f}, PI, Random01(), Random01());
            Vector3 p = Vector3Add(pos, Vector3Scale(dir, scale * (0.45f + 0.35f * t01)));

            SpawnParticle((ParticleConfig){
                .position = p,
                // Rising and spreading: heat, not debris.
                .velocity = { dir.x * scale * 0.35f,
                              scale * (0.55f + 0.4f * Random01()),
                              dir.z * scale * 0.35f },
                .radius   = scale * (0.020f + 0.022f * Random01()),
                .lifetime = Math_Mix(0.45f, 0.95f, Random01()),
                // The edge band's colour, whitened at the source so the boost
                // has headroom to push into (a saturated hue cannot reach white
                // however hard it is multiplied — see VC_Whiten).
                .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.35f),
                                           (unsigned char)(120.0f + 135.0f * Random01())),
                .colorEnd   = VC_WithAlpha(m->glow, 0),
                .render.blendMode = VFX_BLEND_ADDITIVE,   // embers emit...
                .render.unlit     = 1,                    // ...so no lighting multiply
                .render.emissiveBoost = 4.0f,
                .render.trailLength     = 6,
                .render.trailOnly       = 1,
                .render.trailSmooth     = 1,
                .render.trailStepTime   = 0.04f,
                .render.trailWidthRatio = 1.8f,
                .render.trailColorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.35f), 200),
                .render.trailColorEnd   = VC_WithAlpha(m->glow, 0),
            });
        }
    }

    // A dying effect still lights its surroundings while its edge is hot; the
    // light fades with the front rather than with t01, or the last frame would
    // be the brightest.
    static float s_lightAccum = 0.0f;
    s_lightAccum += GetFrameTime();
    if (s_lightAccum >= 0.07f)
    {
        s_lightAccum = 0.0f;
        float front = 4.0f * t01 * (1.0f - t01);
        VFXLight_Spawn(pos, m->soft, scale * (0.8f + 1.2f * front), 0.09f,
                       VFX_PRIORITY_LOW);
    }
}
