// ── P5. VFX_ComposeShockRing — the expanding ring, OFF the ground ────────────
//
// `VFX_ComposeGroundWave` conforms to terrain: it raycasts the map, it lifts
// itself off the surface, and its whole cross-section is a lip standing UP out of
// the ground. A ring in mid-air — an impact in the air, a parry, a barrier
// breaking — shares none of that.
//
// The swept lens keeps the crest on the ring plane and raises the wake.
// Most presets draw both faces for edge-on readability. Concussive uses
// one curved face so its folds appear once.

#define SHOCK_MAX_SLICES 96
#define SHOCK_RADIALS      13
#define SHOCK_CREST_U      0.5f
#define SHOCK_CREST_K      1.0f
#define SHOCK_CORE_RATIO   0.22f
#define SHOCK_CANVAS_MUL   5.2f
#define SHOCK_THICK_RATIO  0.24f
#define SHOCK_FLARE_RATIO  0.12f
#define SHOCK_FLARE_K      1.35f
#define SHOCK_SHADE_FLOOR  0.30f
#define SHOCK_FACE_DIM     0.60f
#define SHOCK_GROUND_LIFT  0.05f

typedef struct
{
    float radiusEase, arrive, fadeK;
    float coreRatio, canvasMul, flareRatio, thickRatio, hole;
    float emissionGain, whiten;
} ShockRingStyle;

static const ShockRingStyle k_shockStyles[VFX_SHOCK_RING_VARIANT_COUNT] = {
    [VFX_SHOCK_RING_IMPACT] = {
        .radiusEase = 3.8f, .arrive = 0.08f, .fadeK = 1.8f,
        .coreRatio = SHOCK_CORE_RATIO, .canvasMul = SHOCK_CANVAS_MUL,
        .flareRatio = SHOCK_FLARE_RATIO, .thickRatio = SHOCK_THICK_RATIO, .hole = 0.12f,
        .emissionGain = 5.10f, .whiten = 0.04f,
    },
    [VFX_SHOCK_RING_PARRY] = {
        .radiusEase = 7.0f, .arrive = 0.035f, .fadeK = 2.4f,
        .coreRatio = 0.16f, .canvasMul = 4.0f,
        .flareRatio = 0.045f, .thickRatio = 0.13f, .hole = 0.11f,
        .emissionGain = 5.60f, .whiten = 0.16f,
    },
    [VFX_SHOCK_RING_SONIC] = {
        .radiusEase = 5.0f, .arrive = 0.05f, .fadeK = 1.6f,
        .coreRatio = 0.18f, .canvasMul = 4.6f,
        .flareRatio = 0.07f, .thickRatio = 0.15f, .hole = 0.08f,
        .emissionGain = 1.35f, .whiten = 0.20f,
    },
    [VFX_SHOCK_RING_PULSE] = {
        .radiusEase = 1.6f, .arrive = 0.12f, .fadeK = 1.2f,
        .coreRatio = 0.29f, .canvasMul = 4.6f,
        .flareRatio = 0.055f, .thickRatio = 0.14f, .hole = 0.04f,
        .emissionGain = 1.24f, .whiten = 0.02f,
    },
    [VFX_SHOCK_RING_CONCUSSIVE] = {
        .radiusEase = 2.8f, .arrive = 0.06f, .fadeK = 1.4f,
        .coreRatio = 0.32f, .canvasMul = 5.4f,
        .flareRatio = 0.17f, .thickRatio = 0.30f, .hole = 0.065f,
        .emissionGain = 2.85f, .whiten = 0.03f,
    },
};

static const char *k_shockVariantNames[VFX_SHOCK_RING_VARIANT_COUNT] = {
    "IMPACT", "PARRY", "SONIC", "PULSE", "CONCUSSIVE"
};

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int emission;
    int t01;
    int seed;
    int hole;
    int premultiply;
    int variant;
    int direction;
    int arc;
} ShockRingShader;

static ShockRingShader s_shockShader = {0};
static bool s_shockInit = false;
static float s_shockBand = 1.0f;
static float s_shockThick = 1.0f;
static float s_shockAlpha = 1.0f;
static float s_shockHole = 0.16f;

static void ShockRing_InitShared(void)
{
    if (s_shockInit)
        return;
    Tuning_RegisterFloat("shock_band", &s_shockBand, 1.0f);
    Tuning_RegisterFloat("shock_thick", &s_shockThick, 1.0f);
    Tuning_RegisterFloat("shock_alpha", &s_shockAlpha, 1.0f);
    Tuning_RegisterFloat("shock_hole", &s_shockHole, 0.16f);
    s_shockInit = true;
}

static void ShockRing_InitShader(void)
{
    if (s_shockShader.shader.id != 0) return;
    s_shockShader.shader = ResourceManager_LoadShader("core/shaders/shock_ring.vs",
                                                      "core/shaders/shock_ring.fs");
    if (s_shockShader.shader.id == 0) return;
    s_shockShader.bodyColor   = GetShaderLocation(s_shockShader.shader, "u_bodyColor");
    s_shockShader.glowColor   = GetShaderLocation(s_shockShader.shader, "u_glowColor");
    s_shockShader.opacity     = GetShaderLocation(s_shockShader.shader, "u_opacity");
    s_shockShader.emission    = GetShaderLocation(s_shockShader.shader, "u_emission");
    s_shockShader.t01         = GetShaderLocation(s_shockShader.shader, "u_t01");
    s_shockShader.seed        = GetShaderLocation(s_shockShader.shader, "u_seed");
    s_shockShader.hole        = GetShaderLocation(s_shockShader.shader, "u_hole");
    s_shockShader.premultiply = GetShaderLocation(s_shockShader.shader, "u_premultiply");
    s_shockShader.variant     = GetShaderLocation(s_shockShader.shader, "u_variant");
    s_shockShader.direction   = GetShaderLocation(s_shockShader.shader, "u_direction");
    s_shockShader.arc         = GetShaderLocation(s_shockShader.shader, "u_arc");
}

static bool ShockRing_HasShader(void)
{
    return s_shockShader.shader.id != 0 && s_shockShader.bodyColor >= 0 &&
           s_shockShader.glowColor >= 0 && s_shockShader.opacity >= 0 &&
           s_shockShader.emission >= 0 && s_shockShader.t01 >= 0 && s_shockShader.seed >= 0 &&
           s_shockShader.hole >= 0 && s_shockShader.premultiply >= 0 &&
           s_shockShader.variant >= 0 && s_shockShader.direction >= 0 &&
           s_shockShader.arc >= 0;
}

// ── Arithmetic mirrored by core/tests/shock_ring_test.c ──────────────────────

static float ShockRing_Radius01Style(float t01, const ShockRingStyle *st, bool inward)
{
    if (inward)
    {
        if (t01 <= 0.0f) return 1.0f;
        if (t01 >= 1.0f) return 0.0f;
        return 1.0f - powf(t01, st->radiusEase * 0.7f);
    }
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, st->radiusEase);
}

static float ShockRing_CoreWidthStyle(float radiusNow, const ShockRingStyle *st)
{
    return radiusNow * st->coreRatio * s_shockBand;
}

static float ShockRing_CanvasWidthStyle(float radiusNow, const ShockRingStyle *st)
{
    return ShockRing_CoreWidthStyle(radiusNow, st) * st->canvasMul;
}

static float ShockRing_HalfThicknessStyle(float coreWidth, float t01, const ShockRingStyle *st)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float rise = SmoothStep01(t01 / 0.10f);
    float thin = powf(1.0f - t01, 1.1f);
    return coreWidth * st->thickRatio * rise * thin * s_shockThick;
}

static float ShockRing_Flare(float u)
{
    if (u >= SHOCK_CREST_U) return 0.0f;
    float x = (SHOCK_CREST_U - u) / SHOCK_CREST_U;
    return powf(x, SHOCK_FLARE_K);
}

static float ShockRing_FlareHeightStyle(float canvasWidth, float t01, const ShockRingStyle *st)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float open = SmoothStep01(t01 / 0.45f);
    return canvasWidth * st->flareRatio * open * s_shockThick;
}

static float ShockRing_Profile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return sinf(PI * powf(u, SHOCK_CREST_K));
}

static float ShockRing_Shade(float u)
{
    float h = ShockRing_Profile(u);
    float face = (u <= SHOCK_CREST_U)
                     ? 1.0f
                     : Math_Mix(1.0f, SHOCK_FACE_DIM,
                                (u - SHOCK_CREST_U) / (1.0f - SHOCK_CREST_U));
    return Math_Mix(SHOCK_SHADE_FLOOR, 1.0f, h) * face;
}

static float ShockRing_Arrival01(float t01, float arrive)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return SmoothStep01(t01 / fmaxf(arrive, 0.01f));
}

static int ShockRing_Slices(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_ULTRA:
    case GFX_HIGH: return SHOCK_MAX_SLICES;
    case GFX_MED:  return 64;
    case GFX_LOW:  return 40;
    default:       return 16;
    }
}

VFX_ShockRingDesc VFX_ShockRing_DefaultDesc(VFX_ShockRingVariant variant)
{
    if (variant < 0 || variant >= VFX_SHOCK_RING_VARIANT_COUNT)
        variant = VFX_SHOCK_RING_IMPACT;
    VFX_ShockRingDesc d = {0};
    d.variant = variant;
    d.intensity = 1.0f;
    d.thickness = 1.0f;
    d.arcSpan = 2.0f * PI;
    d.arcForward = (Vector3){ 0.0f, 0.0f, 1.0f };
    d.inward = false;
    d.seed = 0;
    return d;
}

const char *VFX_ShockRingVariant_Name(VFX_ShockRingVariant variant)
{
    if (variant < 0 || variant >= VFX_SHOCK_RING_VARIANT_COUNT)
        return "IMPACT";
    return k_shockVariantNames[variant];
}

int VFX_ShockRing_GetParams(VFX_ShockRingDesc *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Preset", .group = "Shock Ring", .type = VFX_PARAM_ENUM,
        .valPtr = (void *)&cfg->variant,
        .enumNames = k_shockVariantNames,
        .enumCount = VFX_SHOCK_RING_VARIANT_COUNT,
        .minInt = 0, .maxInt = VFX_SHOCK_RING_VARIANT_COUNT - 1, .stepFloat = 1.0f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Intensity", .group = "Shock Ring", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->intensity, .minFloat = 0.25f, .maxFloat = 2.5f, .stepFloat = 0.1f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Thickness", .group = "Shock Ring", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->thickness, .minFloat = 0.25f, .maxFloat = 2.5f, .stepFloat = 0.1f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Arc Span", .group = "Shock Ring", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->arcSpan, .minFloat = 0.5f, .maxFloat = 2.0f * PI, .stepFloat = 0.25f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Inward", .group = "Shock Ring", .type = VFX_PARAM_BOOL,
        .valPtr = (void *)&cfg->inward
    };

    return n;
}

// ── Shader uniform upload ────────────────────────────────────────────────────

static void ShockRing_SetUniforms(const VFX_ElementMaterial *m, const ShockRingStyle *st,
                                  int variant, float opacity, float emission, float t01, float seed,
                                  float premultiply, float dirSign,
                                  float arcCenterU, float arcHalfSpanU)
{
    Vector4 body = ColorNormalize(m->body);
    Vector4 glow = ColorNormalize(VC_Whiten(m->glow, st->whiten));
    float hole = st->hole * (s_shockHole / 0.16f);
    Vector2 arcVec = { arcCenterU, arcHalfSpanU };

    SetShaderValue(s_shockShader.shader, s_shockShader.bodyColor, &body, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.glowColor, &glow, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.opacity, &opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.emission, &emission, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.t01, &t01, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.seed, &seed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.hole, &hole, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.premultiply, &premultiply, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.variant, &variant, SHADER_UNIFORM_INT);
    SetShaderValue(s_shockShader.shader, s_shockShader.direction, &dirSign, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.arc, &arcVec, SHADER_UNIFORM_VEC2);
}

// One sweep of the lens at `sgn` * offset over [uStart, uEnd].
static void ShockRing_SweepFace(Vector3 center, Vector3 n, Vector3 axA, Vector3 axB,
                                const float *ringRad, const float *ringOff,
                                const Color *ringCol, int radials, int slices,
                                float uStart, float uEnd, float sgn, bool clampGround,
                                float billowStrength, float phase)
{
    static Vector3 prevRing[SHOCK_RADIALS];
    static Vector3 curRing[SHOCK_RADIALS];

    for (int s = 0; s <= slices; s++)
    {
        float u1 = uStart + (uEnd - uStart) * ((float)s / (float)slices);
        float u0 = uStart + (uEnd - uStart) * ((float)(s - 1) / (float)slices);
        float ang = u1 * 2.0f * PI;
        float ca = cosf(ang), sa = sinf(ang);
        float loft = 1.0f;
        if (billowStrength > 0.0f)
            loft += billowStrength * (0.66f * sinf(ang * 5.0f + phase) +
                                     0.34f * sinf(ang * 13.0f - phase * 1.9f));

        for (int i = 0; i < radials; i++)
        {
            Vector3 p = Vector3Add(center,
                                   Vector3Add(Vector3Scale(axA, ca * ringRad[i]),
                                              Vector3Scale(axB, sa * ringRad[i])));
            float off = (sgn < 0.0f && clampGround) ? fminf(ringOff[i], SHOCK_GROUND_LIFT * 0.8f) : ringOff[i];
            curRing[i] = Vector3Add(p, Vector3Scale(n, sgn * off * loft));
        }

        if (s > 0)
        {
            for (int i = 0; i < radials - 1; i++)
            {
                float v0 = (float)i / (float)(radials - 1);
                float v1 = (float)(i + 1) / (float)(radials - 1);
                Color c0 = ringCol[i], c1 = ringCol[i + 1];
                rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                rlTexCoord2f(u0, v0);
                rlVertex3f(prevRing[i].x, prevRing[i].y, prevRing[i].z);
                rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                rlTexCoord2f(u0, v1);
                rlVertex3f(prevRing[i + 1].x, prevRing[i + 1].y, prevRing[i + 1].z);
                rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                rlTexCoord2f(u1, v1);
                rlVertex3f(curRing[i + 1].x, curRing[i + 1].y, curRing[i + 1].z);
                rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                rlTexCoord2f(u1, v0);
                rlVertex3f(curRing[i].x, curRing[i].y, curRing[i].z);
            }
        }
        for (int i = 0; i < radials; i++) prevRing[i] = curRing[i];
    }
}

void VFX_ComposeShockRingEx(Vector3 center, Vector3 normal, VC_MaterialId mat,
                            float radius, float t01, const VFX_ShockRingDesc *desc)
{
    ShockRing_InitShared();
    ShockRing_InitShader();
    if (radius <= 0.0f) radius = 3.0f;
    if (t01 <= 0.0f || t01 >= 1.0f) return;

    VFX_ShockRingDesc d = desc ? *desc : VFX_ShockRing_DefaultDesc(VFX_SHOCK_RING_IMPACT);
    if (d.variant < 0 || d.variant >= VFX_SHOCK_RING_VARIANT_COUNT)
        d.variant = VFX_SHOCK_RING_IMPACT;
    const ShockRingStyle *st = &k_shockStyles[d.variant];

    float intensity = (d.intensity > 0.01f) ? d.intensity : 1.0f;
    float thickness = (d.thickness > 0.01f) ? d.thickness : 1.0f;

    if (Vector3LengthSqr(normal) < 1e-8f)
        normal = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 n = Vector3Normalize(normal);
    Vector3 axA, axB;
    VC_PlaneFrame(n, &axA, &axB);

    bool horizontal = fabsf(n.y) > 0.7f;
    if (horizontal)
        center = Vector3Add(center, Vector3Scale(n, SHOCK_GROUND_LIFT));

    float arrival = ShockRing_Arrival01(t01, st->arrive) * intensity * s_shockAlpha;
    if (arrival <= 0.004f) return;

    float rNow = radius * ShockRing_Radius01Style(t01, st, d.inward);
    float core = ShockRing_CoreWidthStyle(rNow, st);
    float canvas = ShockRing_CanvasWidthStyle(rNow, st);
    float half = ShockRing_HalfThicknessStyle(core, t01, st) * thickness;
    float flareH = ShockRing_FlareHeightStyle(canvas, t01, st) * thickness;
    if (core <= 0.001f) return;

    float seed = (d.seed != 0)
        ? fmodf((float)(d.seed & 0xFFFFu) * 0.6180339f + 1.0f, 17.0f)
        : fmodf(fabsf(center.x * 13.7f + center.y * 3.1f + center.z * 7.3f), 17.0f);

    float arcSpan = (d.arcSpan > 0.0f) ? d.arcSpan : (2.0f * PI);
    bool isFull = (arcSpan >= 2.0f * PI - 0.01f);
    float arcCenterU = 0.0f;
    float arcHalfSpanU = 1.0f;
    float uStart = 0.0f;
    float uEnd = 1.0f;
    int slices = ShockRing_Slices();
    if (!isFull)
    {
        float fa = Vector3DotProduct(d.arcForward, axA);
        float fb = Vector3DotProduct(d.arcForward, axB);
        float ang = (fa * fa + fb * fb > 1e-6f) ? atan2f(fb, fa) : 0.0f;
        if (ang < 0.0f) ang += 2.0f * PI;
        arcCenterU = ang / (2.0f * PI);
        arcHalfSpanU = fmaxf(arcSpan, 0.001f) / (4.0f * PI);
        float marginHalf = fminf(arcHalfSpanU * 1.12f, 0.5f);
        uStart = arcCenterU - marginHalf;
        uEnd = arcCenterU + marginHalf;
        slices = (int)ceilf((float)slices * (uEnd - uStart));
        if (slices < 8) slices = 8;
    }

    const VFX_ElementMaterial *m = VFX_Material(mat);
    const int radials = SHOCK_RADIALS;

    float fallbackAlpha = Clamp(arrival * powf(1.0f - t01, st->fadeK), 0.0f, 1.0f);
    static Color ringCol[SHOCK_RADIALS];
    static float ringOff[SHOCK_RADIALS];
    static float ringRad[SHOCK_RADIALS];
    for (int i = 0; i < radials; i++)
    {
        float u = (float)i / (float)(radials - 1);
        float h = ShockRing_Profile(u);
        Color c = VC_MixColor(m->body, m->glow, h);
        ringCol[i] = VC_WithAlpha(c, (unsigned char)(fallbackAlpha * ShockRing_Shade(u) * 255.0f));
        float flareU = d.inward ? (1.0f - u) : u;
        ringOff[i] = (half + flareH) * ShockRing_Flare(flareU);
        ringRad[i] = rNow + canvas * (u - SHOCK_CREST_U);
        if (ringRad[i] < 0.0f) ringRad[i] = 0.0f;
    }

    float dirSign = d.inward ? -1.0f : 1.0f;
    bool shaded = ShockRing_HasShader();
    int passes = shaded ? 2 : 1;
    // Draw the dense pressure rolls once to avoid projecting overlapping folds.
    bool concussive = d.variant == VFX_SHOCK_RING_CONCUSSIVE;
    int faceCount = shaded && concussive ? 1 : 2;
    float billowStrength = shaded && concussive ? 0.36f : 0.0f;
    float billowPhase = seed + t01 * 1.23f;
    for (int pass = 0; pass < passes; pass++)
    {
        VFXRenderScope renderScope = VFXRender_BeginDraw(
            pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
            pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        if (shaded)
        {
            SkillManager_BeginShader(s_shockShader.shader);
            ShockRing_SetUniforms(m, st, d.variant,
                                  (pass == 0) ? arrival * 0.92f : arrival * 0.86f,
                                  (pass == 0) ? 1.0f : st->emissionGain,
                                  t01, seed, (pass == 0) ? 0.0f : 1.0f, dirSign,
                                  arcCenterU, arcHalfSpanU);
        }
        rlBegin(RL_QUADS);
        for (int side = 0; side < faceCount; side++)
        {
            float sgn = (side == 0) ? 1.0f : -1.0f;
            ShockRing_SweepFace(center, n, axA, axB, ringRad, ringOff, ringCol,
                                radials, slices, uStart, uEnd, sgn, horizontal, billowStrength, billowPhase);
        }
        rlEnd();
        rlDrawRenderBatchActive();
        if (shaded) SkillManager_EndShader();

        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&renderScope);
    }
}

void VFX_ComposeShockRing(Vector3 center, Vector3 normal, VC_MaterialId mat,
                          float radius, float t01)
{
    VFX_ComposeShockRingEx(center, normal, mat, radius, t01, NULL);
}

// ── Pooled one-shot wrapper for Update/Cast callers ──────────────────────────
#define VC_SHOCK_RING_POOL_MAX 16
typedef struct {
    bool active;
    Vector3 center;
    Vector3 normal;
    VC_MaterialId mat;
    float radius;
    float elapsed;
    float duration;
    VFX_ShockRingDesc desc;
} VC_ShockRingPoolSlot;

static VC_ShockRingPoolSlot s_srPool[VC_SHOCK_RING_POOL_MAX] = {0};
static unsigned int s_srSeedSalt = 1u;

int VFX_SpawnShockRingEx(Vector3 center, Vector3 normal, VC_MaterialId mat,
                          float radius, float duration, const VFX_ShockRingDesc *desc)
{
    int slot = -1;
    int oldestSlot = 0;
    float maxProgress = -1.0f;
    for (int i = 0; i < VC_SHOCK_RING_POOL_MAX; i++) {
        if (!s_srPool[i].active) { slot = i; break; }
        float p = s_srPool[i].elapsed / fmaxf(s_srPool[i].duration, 0.001f);
        if (p > maxProgress) { maxProgress = p; oldestSlot = i; }
    }
    if (slot < 0) slot = oldestSlot;

    s_srPool[slot].active = true;
    s_srPool[slot].center = center;
    s_srPool[slot].normal = (Vector3LengthSqr(normal) > 1e-5f) ? Vector3Normalize(normal) : (Vector3){ 0.0f, 1.0f, 0.0f };
    s_srPool[slot].mat = mat;
    s_srPool[slot].radius = (radius > 0.1f) ? radius : 1.2f;
    s_srPool[slot].elapsed = 0.0f;
    s_srPool[slot].duration = (duration > 0.06f) ? duration : 0.35f;
    s_srPool[slot].desc = desc ? *desc : VFX_ShockRing_DefaultDesc(VFX_SHOCK_RING_IMPACT);
    if (s_srPool[slot].desc.seed == 0) {
        unsigned int posHash = (unsigned int)fabsf(center.x * 137.0f + center.y * 31.0f + center.z * 73.0f);
        s_srPool[slot].desc.seed = (posHash + (s_srSeedSalt++ * 2654435761u)) | 1u;
    }
    return slot + 1;
}

int VFX_SpawnShockRing(Vector3 center, Vector3 normal, VC_MaterialId mat, float radius, float duration)
{
    return VFX_SpawnShockRingEx(center, normal, mat, radius, duration, NULL);
}

static void VC_ShockRingPool_Update(float dt)
{
    for (int i = 0; i < VC_SHOCK_RING_POOL_MAX; i++) {
        if (!s_srPool[i].active) continue;
        s_srPool[i].elapsed += dt;
        if (s_srPool[i].elapsed >= s_srPool[i].duration) {
            s_srPool[i].active = false;
        }
    }
}

static void VC_ShockRingPool_Draw3D(Camera3D cam)
{
    (void)cam;
    for (int i = 0; i < VC_SHOCK_RING_POOL_MAX; i++) {
        if (!s_srPool[i].active) continue;
        float t01 = Clamp(s_srPool[i].elapsed / s_srPool[i].duration, 0.0f, 1.0f);
        VFX_ComposeShockRingEx(s_srPool[i].center, s_srPool[i].normal, s_srPool[i].mat,
                               s_srPool[i].radius, t01, &s_srPool[i].desc);
    }
}
