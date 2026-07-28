// ── E6.7 — VFX_ComposeLightShaft ─────────────────────────────────────────────
//
// Godrays: light made visible by the dust it passes through.
//
// A light shaft is not a glowing bar. Three properties separate the two, and all
// three are geometric:
//   1. THE SHAFTS CONVERGE AT THE SOURCE. They fan out from one point and widen
//      with distance. A bundle of parallel bars reads as neon tubing.
//   2. THEY DIM WITH DISTANCE, and they dim at the source too — a shaft with a
//      hard bright cap at its origin looks cut, because in the real thing the
//      brightest part is the source itself, which is not this effect's job.
//   3. THEY BREATHE INDEPENDENTLY. Air moves; dust drifts through the cone. Six
//      shafts pulsing on one clock is a lamp, not a volume.
//
// WHAT THE SPEC ASKED FOR THAT IS NOT HERE: "soft-particle depth fade". Soft
// particles are PARKED (ENGINE_LANDMINES, 28/07/2026 — a second sampler in a
// core shader unbinds `texture0` under rlvk, and this composition would need
// exactly that second sampler for the depth texture). Adding it here would break
// this effect's own texture in the same way. Instead the far end fades out on its
// own alpha envelope, so a shaft that runs into geometry is already dim where it
// meets it. That is a weaker cure — it fades by DISTANCE ALONG THE SHAFT rather
// than by proximity to whatever it hits — and it is the honest one available
// until the sampler problem is solved in `third_party/vulkan/`.
//
// Brightness: the spec wants this over the bloom threshold so E1's streak bloom
// does the work. `main.c:1139` sets `bloomThreshold = 0.8` on LUMA, and ribbon
// vertex colour is 8-bit and caps at 1.0, so a single pass cannot get there
// reliably. Each shaft therefore draws a wide soft pass and a narrow hot one; the
// core is where the luma clears 0.8 and the bloom takes over.

#define LIGHT_SHAFT_MAX     8    // shafts per call
#define LIGHT_SHAFT_POINTS  10   // samples along one shaft

static Texture2D s_shaftTex  = {0};
static bool      s_shaftInit = false;

static float s_shaftCount  = 6.0f;   // how many shafts (clamped to MAX)
static float s_shaftSpread = 1.0f;   // x on the cone's opening
static float s_shaftGain   = 1.0f;   // x on alpha

// Cross-shaft profile: a plain gaussian, and SYMMETRIC on purpose. The slash's
// mask is asymmetric because a blade has an edge; a shaft of light has a middle.
// Symmetric also means the passes can share a centre — the outer-edge alignment
// SweepSlash needs does not apply here (core/docs/LANDMINES.md).
static void LightShaft_BuildTexture(void)
{
    const int W = 64, H = 64;
    Image img = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++)
    {
        float v = ((float)y + 0.5f) / (float)H;      // along the shaft
        for (int x = 0; x < W; x++)
        {
            float u = ((float)x + 0.5f) / (float)W;  // across the shaft
            float d = (u - 0.5f) / 0.26f;
            float a = expf(-d * d);
            // A slow lengthwise ripple: dust is not evenly distributed, and a
            // perfectly even shaft reads as a solid object. Shallow — deep
            // enough to see is deep enough to look like a dashed line.
            a *= 0.85f + 0.15f * sinf(v * PI * 3.0f + u * 4.0f);
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255,
                                               (unsigned char)(Clamp(a, 0.0f, 1.0f) * 255.0f)});
        }
    }
    s_shaftTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_shaftTex.id != 0)
    {
        SetTextureFilter(s_shaftTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_shaftTex, TEXTURE_WRAP_CLAMP);
    }
}

static void LightShaft_InitShared(void)
{
    if (s_shaftInit) return;
    LightShaft_BuildTexture();
    Tuning_RegisterFloat("shaft_count",  &s_shaftCount,  6.0f);
    Tuning_RegisterFloat("shaft_spread", &s_shaftSpread, 1.0f);
    Tuning_RegisterFloat("shaft_gain",   &s_shaftGain,   1.0f);
    s_shaftInit = true;
}

// Alpha along one shaft, 0 at the source end, 1 at the far end.
// Rises out of the source (no hard cap) and falls away with distance.
static float LightShaft_Alpha(float t)
{
    float in  = SmoothStep01(t / 0.16f);            // out of the source
    float out = 1.0f - SmoothStep01((t - 0.35f) / 0.65f);
    return in * out;
}

// Continuous: call every frame while the light is up.
// `from` = the source (shafts converge here), `to` = where the cone points.
// `width` = the cone's FULL width at `to`, metres. `intensity` 0..1.
void VFX_ComposeLightShaft(Vector3 from, Vector3 to, VC_MaterialId mat,
                           float width, float intensity)
{
    LightShaft_InitShared();
    if (width <= 0.0f) width = 1.0f;
    float ity = Clamp(intensity, 0.0f, 1.0f);
    if (ity <= 0.001f) return;

    Vector3 axis = Vector3Subtract(to, from);
    float   len  = Vector3Length(axis);
    if (len < 0.01f) return;
    axis = Vector3Scale(axis, 1.0f / len);

    // A basis across the cone. Same pole guard as VC_DirCone / Rune_PlaneBasis:
    // the reference axis is swapped near the pole so the cross never degenerates.
    Vector3 ref = (fabsf(axis.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                          : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 bx = Vector3Normalize(Vector3CrossProduct(axis, ref));
    Vector3 by = Vector3CrossProduct(axis, bx);

    int shafts = (int)(s_shaftCount + 0.5f);
    if (shafts < 1) shafts = 1;
    if (shafts > LIGHT_SHAFT_MAX) shafts = LIGHT_SHAFT_MAX;

    // E8 tier budget. Cost here is shafts x passes x overdraw, all of it
    // large-area additive — the single most fill-hungry thing Đợt E added. LOW
    // halves the count and drops the wide soft pass, keeping the hot cores: a
    // thinner cone, still a cone. The narrow pass is the one kept because it is
    // both the cheaper and the one carrying the bloom.
    int passStart = 0, passCount = 2;
    if (GfxQuality_Get() <= GFX_LOW)
    {
        shafts = (shafts + 1) / 2;
        passStart = 1;                 // skip the wide soft pass
    }
    (void)passCount;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Color soft = m->soft;
    Color glow = m->glow;
    float time = (float)GetTime();

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDrawRenderBatchActive();

    static RibbonPoint pts[LIGHT_SHAFT_POINTS];

    // Wide-and-soft, then narrow-and-hot. The narrow pass is what clears the
    // bloom threshold; the wide one is the volume around it.
    static const float passW[2]     = {1.0f, 0.34f};
    static const float passA[2]     = {0.55f, 1.0f};
    static const float passWhite[2] = {0.15f, 0.85f};

    for (int sIdx = 0; sIdx < shafts; sIdx++)
    {
        // Deterministic, closed-form placement around the cone — NOT random per
        // frame (a shaft that moves every frame flickers) and NOT evenly spaced
        // (an even fan reads as a machine part). The golden angle spreads them
        // without any two landing near each other.
        float g    = (float)sIdx * 2.39996f;
        float rad  = sqrtf(((float)sIdx + 0.5f) / (float)shafts);
        float offX = cosf(g) * rad, offY = sinf(g) * rad;

        // Each shaft on its own clock (see the header note: one clock = a lamp).
        float breathe = VC_Breathe(time + (float)sIdx * 1.7f, 0.55f + 0.11f * (float)sIdx, 0.22f);
        float halfW   = width * 0.5f * 0.34f * s_shaftSpread * breathe;

        for (int pass = passStart; pass < 2; pass++)
        {
            for (int i = 0; i < LIGHT_SHAFT_POINTS; i++)
            {
                float t = (float)i / (float)(LIGHT_SHAFT_POINTS - 1); // 0 source → 1 far

                // THE CONE. The lateral offset scales with `t`, so every shaft
                // passes through `from` and they separate with distance. This one
                // line is the difference between godrays and a bundle of bars.
                float spread = width * 0.5f * s_shaftSpread * t;
                Vector3 p = Vector3Add(from, Vector3Scale(axis, len * t));
                p = Vector3Add(p, Vector3Scale(bx, offX * spread));
                p = Vector3Add(p, Vector3Scale(by, offY * spread));

                pts[i].position = p;
                // Widens with distance, like anything diverging from a point.
                pts[i].halfWidth = halfW * passW[pass] * Math_Mix(0.35f, 1.0f, t);

                float a = LightShaft_Alpha(t) * passA[pass] * ity * s_shaftGain
                          * breathe;
                // Hot at the source, cooling into the material's pastel with
                // distance — the same "hue travels" rule the slash uses.
                Color c = VC_Whiten(VC_MixColor(glow, soft, SmoothStep01(t)),
                                    passWhite[pass]);
                pts[i].tint = ColorAlpha(c, Clamp(a, 0.0f, 1.0f));
            }

            Ribbon_ComputeArcLengthUV(pts, LIGHT_SHAFT_POINTS);
            // CAMERA_FACING, per the spec: a shaft is a volume seen from
            // wherever the viewer is, so its silhouette must always face them.
            DrawRibbonStrip(pts, LIGHT_SHAFT_POINTS, s_shaftTex, camera);
        }
    }

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
}
