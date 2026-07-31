// P4 — ContactSpark. A bright central core firing straight radial wisp trails.
#include "core/tuning.h"

#define CONTACT_SPARK_MAX 12
static bool  s_contactSparkInit = false;
static float s_contactSparkCount = 1.0f;
static float s_contactSparkScale = 1.0f;

static void ContactSpark_Init(void)
{
    if (s_contactSparkInit) return;
    Tuning_RegisterFloat("contact_spark_count", &s_contactSparkCount, 1.0f);
    Tuning_RegisterFloat("contact_spark_scale", &s_contactSparkScale, 1.0f);
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
    SparkTrail_InitShared();
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
        cfg.len = length; cfg.thick = length * SPARK_TRAIL_ASPECT;
        cfg.trailLength = SPARK_TRAIL_NODES;
        cfg.life = Math_Mix(0.35f, 0.60f, Random01());
        cfg.tint = VC_WithAlpha(VC_Whiten(mat->glow, 0.64f), 250);
        cfg.forceField = NULL;                 // straight radial flight
        cfg.widthCurve = &s_sparkWidth; cfg.alphaCurve = &s_sparkAlpha;
        cfg.smoothSpline = true; cfg.blendMode = BLEND_ADDITIVE;
        cfg.ribbonMode = RIBBON_CAMERA_FACING; cfg.disableInnerCore = true;
        cfg.priority = VFX_PRIORITY_LOW;
        (void)SpawnTrailEntity(cfg);
    }
}
