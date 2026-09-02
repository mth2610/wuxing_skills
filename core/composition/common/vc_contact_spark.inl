// P4 — ContactSpark. A bright central core firing straight radial wisp trails.
//
// THE STRAND GEOMETRY BELOW USED TO LIVE IN vc_spark_trail.inl, which was
// deleted on 27/08/2026 at the owner's call ("nó rất xấu"): SPARK TRAIL was a
// primary whose whole subject was ONE additive dash, and at any size that read
// on screen it read as a dash. This file was its only other reader — it used
// the curves and the aspect, never the public entry point — so the pieces moved
// here rather than staying behind in an orphaned shared file. They are renamed
// with them: a SPARK_TRAIL_* constant surviving the effect it was named for is
// exactly the alias-that-changes-meaning the F0 purge rule forbids.
#include "core/tuning.h"

#define CONTACT_SPARK_MAX 28

// History nodes per radial strand — a tail, not a rope.
#define CONTACT_SPARK_NODES 12

// Aspect: a comet/wisp is ~1:14 against its OWN length (core/docs/LANDMINES.md,
// "Thickness is a ratio against the thing's OWN length"). The trail system's
// WISP draw treats `thick` as a HALF-width, so half of 1/14 is 1/28.
#define CONTACT_SPARK_ASPECT (1.0f / 28.0f)

static bool  s_contactSparkInit = false;
static float s_contactSparkCount = 1.0f;
static float s_contactSparkScale = 1.0f;
static SkillCurve s_contactSparkWidth = {0};
static SkillCurve s_contactSparkAlpha = {0};
static ForceField s_contactSparkCentrifugalField = {0};
static ColorGradient s_contactSparkHeat[VC_MAT_COUNT] = {0};
static bool s_contactSparkHeatReady[VC_MAT_COUNT] = {0};

static const ColorGradient *ContactSpark_HeatGradient(VC_MaterialId matId,
                                                       const VFX_ElementMaterial *mat)
{
    int i = (int)matId;
    if (i < 0 || i >= VC_MAT_COUNT) i = VC_MAT_TAIJI;
    if (!s_contactSparkHeatReady[i])
    {
        // Same hot-source → pastel-distance profile as LightShaft. It is
        // deliberately not a guessed black-body palette: each material's
        // glow/soft pair is already calibrated together by that composition.
        ColorGradient_AddStop(&s_contactSparkHeat[i], 0.00f,
                              VC_WithAlpha(VC_Whiten(mat->glow, 0.85f), 255));
        ColorGradient_AddStop(&s_contactSparkHeat[i], 0.24f,
                              VC_WithAlpha(VC_Whiten(VC_MixColor(mat->glow, mat->soft, 0.24f), 0.55f), 255));
        ColorGradient_AddStop(&s_contactSparkHeat[i], 0.62f,
                              VC_WithAlpha(VC_Whiten(VC_MixColor(mat->glow, mat->soft, 0.62f), 0.15f), 230));
        ColorGradient_AddStop(&s_contactSparkHeat[i], 1.00f,
                              VC_WithAlpha(mat->soft, 0));
        s_contactSparkHeatReady[i] = true;
    }
    return &s_contactSparkHeat[i];
}

static void ContactSpark_Init(void)
{
    if (s_contactSparkInit) return;
    Tuning_RegisterFloat("contact_spark_count", &s_contactSparkCount, 1.0f);
    Tuning_RegisterFloat("contact_spark_scale", &s_contactSparkScale, 1.0f);

    // BOTH ENDS COME TO A POINT. The WISP type's built-in taper
    // (ComputeWispStyleTaper) is pointed at the tail and FLAT at the head — it
    // reaches full width by segRatio 0.5 and stays there — so a strand drawn
    // with it ends in a cut-off rectangle at the very place the eye is looking.
    // A lens is the shape (core/docs/LANDMINES.md): widest just behind the
    // head, needle at both tips.
    FloatCurve_AddStop(&s_contactSparkWidth, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_contactSparkWidth, 0.30f, 0.62f);
    FloatCurve_AddStop(&s_contactSparkWidth, 0.80f, 1.00f);
    FloatCurve_AddStop(&s_contactSparkWidth, 1.00f, 0.20f);

    // Brightness rides toward the head: the tail is what is LEFT of the light.
    // It must fall at least as fast as the width does, or the last stretch is
    // sub-pixel while still visible and breaks into dashes (LANDMINES, 29/07).
    FloatCurve_AddStop(&s_contactSparkAlpha, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_contactSparkAlpha, 0.30f, 0.45f);
    FloatCurve_AddStop(&s_contactSparkAlpha, 0.85f, 1.00f);
    FloatCurve_AddStop(&s_contactSparkAlpha, 1.00f, 1.00f);
    s_contactSparkInit = true;
}

// Event — call once at contact. Cost budget: 18..28 low-priority wisp trails,
// each capped at 12 history nodes, plus one additive bloom core.
void VFX_ComposeContactSparkMode(Vector3 pos, VC_MaterialId matId, float scale,
                                 float severity01, ContactSparkMode mode)
{
    ContactSpark_Init();
    if (scale <= 0.0f) return;
    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    int count = (int)((18.0f + severity01 * 10.0f) * s_contactSparkCount);
    if (count < 1) count = 1;
    if (count > CONTACT_SPARK_MAX) count = CONTACT_SPARK_MAX;
    scale *= s_contactSparkScale;
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    const ColorGradient *heat = ContactSpark_HeatGradient(matId, mat);

    for (int i = 0; i < count; ++i) {
        // Uniform directions over a sphere, rather than a ground-plane fan.
        float angle = Random01() * 2.0f * PI;
        float y = Random01() * 2.0f - 1.0f;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        Vector3 dir = {cosf(angle) * r, y, sinf(angle) * r};
        float speed = Math_Mix(4.0f, 9.0f, Random01()) * scale;
        float length = Math_Mix(0.45f, 1.45f, Random01()) * scale;
        TrailConfig cfg = {0};
        cfg.type = TRAIL_TYPE_WISP; cfg.pos = pos;
        cfg.vel = mode == CONTACT_SPARK_CENTRIFUGAL ? Vector3Scale(dir, speed)
                                                    : (Vector3){0.0f, 0.0f, 0.0f};
        cfg.target = Vector3Scale(dir, -1.0f); // history trails behind the head
        cfg.len = length; cfg.thick = length * CONTACT_SPARK_ASPECT;
        cfg.trailLength = CONTACT_SPARK_NODES;
        cfg.life = Math_Mix(0.35f, 0.60f, Random01());
        // Centre → edge: white-hot, yellow, then a cooling orange tail.
        cfg.tint = WHITE;
        cfg.gradient = heat;
        // WISP integrates node velocity only when a field/wind is present. An
        // empty field is therefore intentional: it supplies no acceleration,
        // but lets CENTRIFUGAL strands actually travel outward.
        cfg.forceField = mode == CONTACT_SPARK_CENTRIFUGAL
            ? &s_contactSparkCentrifugalField : NULL;
        cfg.widthCurve = &s_contactSparkWidth; cfg.alphaCurve = &s_contactSparkAlpha;
        cfg.smoothSpline = true; cfg.blendMode = BLEND_ADDITIVE;
        cfg.ribbonMode = RIBBON_CAMERA_FACING; cfg.disableInnerCore = false;
        cfg.priority = VFX_PRIORITY_LOW;
        (void)SpawnTrailEntity(cfg);
    }
}

void VFX_ComposeContactSpark(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    VFX_ComposeContactSparkMode(pos, matId, scale, severity01, CONTACT_SPARK_CENTRIFUGAL);
}
