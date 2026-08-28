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

#define CONTACT_SPARK_MAX 12

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

// Event — call once at contact. Cost budget: 8..12 low-priority wisp trails,
// each capped at 12 history nodes, plus one additive bloom core.
void VFX_ComposeContactSpark(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    ContactSpark_Init();
    if (scale <= 0.0f) return;
    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    int count = (int)((8.0f + severity01 * 4.0f) * s_contactSparkCount);
    if (count < 1) count = 1;
    if (count > CONTACT_SPARK_MAX) count = CONTACT_SPARK_MAX;
    scale *= s_contactSparkScale;
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // One hot point at the origin makes the burst read as a single contact,
    // rather than unrelated lines that happened to cross a position.
    Glint_InitShared();
    SpawnParticle((ParticleConfig){
        .position = pos, .radius = 0.16f * scale, .lifetime = 0.06f,
        .colorStart = VC_WithAlpha(VC_Whiten(mat->glow, 0.82f), 255),
        .colorEnd = VC_WithAlpha(WHITE, 0), .alphaCurve = &s_glintFade,
        .render.texture = s_glintTex, .render.blendMode = VFX_BLEND_ADDITIVE,
        .render.unlit = 1, .render.emissiveBoost = 1.0f,
    });
    for (int i = 0; i < count; ++i) {
        // Uniform directions over a sphere, rather than a ground-plane fan.
        float angle = Random01() * 2.0f * PI;
        float y = Random01() * 2.0f - 1.0f;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        Vector3 dir = {cosf(angle) * r, y, sinf(angle) * r};
        float speed = Math_Mix(6.0f, 12.0f, Random01()) * scale;
        float length = Math_Mix(0.90f, 2.20f, Random01()) * scale;
        TrailConfig cfg = {0};
        cfg.type = TRAIL_TYPE_WISP; cfg.pos = pos;
        cfg.vel = Vector3Scale(dir, speed);
        cfg.target = Vector3Scale(dir, -1.0f); // history trails behind the head
        cfg.len = length; cfg.thick = length * CONTACT_SPARK_ASPECT;
        cfg.trailLength = CONTACT_SPARK_NODES;
        cfg.life = Math_Mix(0.35f, 0.60f, Random01());
        cfg.tint = VC_WithAlpha(VC_Whiten(mat->glow, 0.64f), 250);
        cfg.forceField = NULL;                 // straight radial flight
        cfg.widthCurve = &s_contactSparkWidth; cfg.alphaCurve = &s_contactSparkAlpha;
        cfg.smoothSpline = true; cfg.blendMode = BLEND_ADDITIVE;
        cfg.ribbonMode = RIBBON_CAMERA_FACING; cfg.disableInnerCore = true;
        cfg.priority = VFX_PRIORITY_LOW;
        (void)SpawnTrailEntity(cfg);
    }
}
